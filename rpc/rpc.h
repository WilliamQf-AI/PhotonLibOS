/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once
#include <cassert>
#include <photon/common/stream.h>
#include <photon/common/iovector.h>
#include <photon/common/object.h>
#include <photon/common/callback.h>
#include <photon/net/socket.h>
#include <photon/rpc/serialize.h>

namespace photon {
namespace rpc
{
    union FunctionID
    {
        struct
        {
            uint32_t interface;     // 32-bit interface ID
            uint32_t method;        // 32-bit method ID
        };
        uint64_t function;          // or composed as 64-bit function ID

        FunctionID() { }
        FunctionID(uint64_t F) : function(F) { }
        FunctionID(uint32_t I, uint32_t M) : interface(I), method(M) { }
        operator uint64_t () { return function; }
    };

    struct Header
    {
        const static uint64_t MAGIC   = 0x87de5d02e6ab95c7;
        const static uint32_t VERSION = 0;

        uint64_t magic   = MAGIC;       // the header magic
        uint32_t version = VERSION;     // version of the message
        uint32_t size;                  // size of the payload, not including the header
        FunctionID function;            // function ID, or composition of interface and method
        uint64_t tag;                   // tag of the payload, always increasing
        uint64_t reserved = 0;          // padding to 40 bytes
    };

    class Stub : public Object {
    public:
        /**
         * @brief Stub is the entity to send RPC calls. It is based on socket stream.
         *        A stub can be created via `new_rpc_stub(IStream*)`, and can be retrieved from `StubPool`.
         * @param req Request of Message
         * @param resp Response of Message
         * @param timeout RPC timeout, counting from sending request to receiving response header
         * @return The number of bytes received, -1 for failure
         * @note Request and Response should assign to external memory buffers if they have variable-length fields.
         *       Via this, we can achieve zero-copy send and receive.
         *       For Response, there could be only 1 buffer at most. For Request, there is no limit.
         *       Attention: RPC stub do not support multi vCPU, when multiple vCPUs are used, the RPC stub should be
         *       vCPU local object.
         */
        template<typename Operation>
        int call(typename Operation::Request& req,
                 typename Operation::Response& resp,
                 Timeout timeout = {})
        {
            SerializerIOV reqmsg;
            reqmsg.serialize(req);

            SerializerIOV respmsg;
            respmsg.serialize(resp);
            if (respmsg.iovfull) {
                errno = ENOBUFS;
                return -1;
            }

            ssize_t expected_size = respmsg.iov.sum();
            FunctionID fid(Operation::IID, Operation::FID);
            int ret = do_call(fid, &reqmsg.iov, &respmsg.iov, timeout);
            if (ret < 0) {
                // thread_usleep(10 * 1000); // should be put into do_call(), if necessary
                // LOG_ERROR("failed to perform RPC ", ERRNO());
                return -1;
            }
            if (ret < expected_size) {
                DeserializerIOV des;
                respmsg.iov.truncate(ret);
                using P = typename Operation::Response;
                auto re = des.deserialize<P>(&respmsg.iov);
                if (re == nullptr) return -1;
                // Memory overlap is not supposed to happen
                assert((((char*)re + sizeof(P)) <= (char*)&resp) ||
                    ((char*)re >= ((char*)&resp + sizeof(P))));
                memcpy(&resp, re, sizeof(P));
            } else {
                if (!resp.validate_checksum(&respmsg.iov, nullptr, 0))
                    return -1;
            }
            return ret;
        }

        /**
         * @param req Request of Message
         * @param resp_iov iovector for the Response
         * @param timeout timeout in milliseconds, -1UL for no timeout. 
         * @return Pointer of the Response. nullptr for failure. No need to delete.
         * @note For this call, we don't need to assign buffers for the Response any more.
         *       `resp_iov` will use its internal allocator to fulfill the memory requirement.
         *       The only difference between these two calls is the allocator's overhead.
         */
        template<typename Operation>
        typename Operation::Response* call(typename Operation::Request& req, iovector& resp_iov,
                                            Timeout timeout = {}) {
            assert(resp_iov.iovcnt() == 0);
            SerializerIOV reqmsg;
            reqmsg.serialize(req);

            FunctionID fid(Operation::IID, Operation::FID);
            int ret = do_call(fid, &reqmsg.iov, &resp_iov, timeout);
            if (ret < 0)
                return nullptr;
            DeserializerIOV des;
            return des.deserialize<typename Operation::Response>(&resp_iov);
        }

        virtual IStream* get_stream() = 0;

        virtual int set_stream(IStream*) = 0;

        virtual int get_queue_count() = 0;

    protected:
        // This call can be invoked concurrently, and may return out-of-order.
        // Return the number of bytes received.
        virtual int do_call(FunctionID function, iovector* request, iovector* response, Timeout timeout) = 0;
    };

    /**
     * @brief Skeleton is the entity of RPC server
     */
    class Skeleton : public Object
    {
    public:
        // the function object to send back rpc response
        //`int (XXXX:*)(iovector* response)`, or
        //`int (XXXX*, iovector* response)`;
        typedef ::Callback<iovector*> ResponseSender;

        // the function object to serve a rpc request
        //`int (XXXX:*)(iovector* request, ResponseSender resp_sender)`, or
        //`int (XXXX*,  iovector* request, ResponseSender resp_sender)`
        typedef ::Callback<iovector*, ResponseSender, IStream*> Function;

        typedef ::Callback<IStream*> Notifier;

        virtual int add_function(FunctionID func_id, Function func) = 0;
        virtual int remove_function(FunctionID func_id) = 0;

        virtual int set_accept_notify(Notifier notifier) = 0;
        virtual int set_close_notify(Notifier notifier) = 0;

        /**
         * @brief The main logic to handle incoming RPC calls. Users can invoke this function
         *        inside the custom `Handler` of `ISocketServer`. Multiple socket servers
         *        are supported, for example, `new_tcp_socket_server()`, `new_uds_server()` ...
         * @note Can be invoked concurrently by multiple threads
         */
        virtual int serve(IStream* stream) = 0;

        __attribute__((deprecated))
        int serve(IStream* stream, bool /*ownership_stream*/) {
            return serve(stream);
        }

        // set the allocator to allocate memory for recving responses
        // the default allocator is defined in iovector.h/cpp
        virtual void set_allocator(IOAlloc allocation) = 0;

        /**
         * @brief Shutdown the rpc server from outside.
         * @warning DO NOT invoke this function within the RPC request.
         *          You should create a thread to invoke it, or just use shutdown_no_wait.
         */
        virtual int shutdown(bool no_more_requests = true) = 0;

        virtual int shutdown_no_wait() = 0;

        template <class ServerClass>
        int register_service(ServerClass* obj)
        {
            return obj == nullptr ? -1 : 0;
        }

        template <typename Operation, typename... Operations, class ServerClass>
        int register_service(ServerClass* obj)
        {
            int ret = register_service<Operations...>(obj);
            if (ret < 0)
                return ret;
            FunctionID fid(Operation::IID, Operation::FID);
            Function func((void*)obj, &rpc_service<Operation, ServerClass>);
            return add_function(fid, func);
        }

    protected:
        template <typename Operation, class ServerClass>
        static int rpc_service(void* obj, iovector* req, ResponseSender rs, IStream* stream)
        {
            using Request = typename Operation::Request;
            using Response = typename Operation::Response;

            DeserializerIOV reqmsg;
            auto request = reqmsg.deserialize<Request>(req);
            if (!request) { errno = EINVAL; return -1; }    // failed to decode

            IOVector iov;
            iov.allocator = *req->get_allocator();
            Response response;
            // some service (like preadv) may need an iovector
            // invoke actual service function in ServerClass by overloading
            auto fini = static_cast<ServerClass*>(obj) ->
                do_rpc_service(request, &response, &iov, stream);
            (void)fini; // To prevent possible compiler warning about unused variable.
                        // Note that `fini` (of any type) may get destructed after sending,
                        // giving a chance for the `Operation` to do some cleaning up.
            SerializerIOV respmsg;
            respmsg.serialize(response);
            return rs(&respmsg.iov);
        }
    };

    /**
     * @brief StubPool is the general user-interface of RPC client. It uses an ObjectCache
     *        to manage multiple Stubs based on their network Endpoints.
     *
     * @note About timeout:
     *  1. When a socket/stub not used by any caller for `expiration` microsecs, it will be dropped.
     *  2. When socket connecting, it will fail by be timed out after `connect_timeout` microsecs.
     *  3. `Stub::call` measures the time from invoking `call` before sending request to received
     *    response head. Receiving response body is not considered.
     */
    class StubPool : public Object {
    public:
        // Get a RPC stub(client) from expire-container, which is a connection pool.
        // If no existing stub was found, a new one will be created.
        virtual Stub* get_stub(const net::EndPoint& endpoint, bool tls) = 0;

        // Put the RPC stub, could destroy the resource `immediately`, otherwise a ref count will be made,
        // and the resource will be cleared later.
        virtual int put_stub(const net::EndPoint& endpoint, bool immediately) = 0;

        // Get an existing stub. Return nullptr if not found.
        virtual Stub* acquire(const net::EndPoint& endpoint) = 0;

        // Get RPC call timeout.
        virtual uint64_t get_timeout() const = 0;
    };

    extern "C" Stub* new_rpc_stub(IStream* stream, bool ownership = false);

    extern "C" StubPool* new_stub_pool(uint64_t expiration,
                                       uint64_t timeout,
                                       std::shared_ptr<net::ISocketClient> socket_client = nullptr);

    extern "C" StubPool* new_uds_stub_pool(const char* path,
                                           uint64_t expiration,
                                           uint64_t timeout);

    extern "C" Skeleton* new_skeleton(uint32_t pool_size = 128);

    __attribute__((deprecated))
    inline Skeleton* new_skeleton(bool /*concurrent*/, uint32_t pool_size) {
        return new_skeleton(pool_size);
    }

}
}
