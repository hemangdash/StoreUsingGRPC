#include <iostream>
#include <thread>
#include <fstream>
#include "threadpool.h"

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>

#include "vendor.grpc.pb.h"
#include "store.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;
using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using store::ProductQuery;
using store::ProductReply;
using store::ProductInfo;
using store::Store;
using vendor::BidQuery;
using vendor::BidReply;
using vendor::Vendor;

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::unique_ptr;
using std::shared_ptr;
using std::ifstream;

string vendor_addresses_file;
string store_address;

class VendorClient {
   public:
      explicit VendorClient(shared_ptr<Channel> channel) 
         : stub(Vendor::NewStub(channel)) {}

      BidReply getBid(const string &product_name) {
         BidQuery bid_query;
         bid_query.set_product_name(product_name);
         BidReply bid_reply;
         ClientContext context;
         CompletionQueue completion_queue;
         Status status;

         unique_ptr<ClientAsyncResponseReader<BidReply>> rpc(stub->AsyncgetProductBid(&context, bid_query, &completion_queue));
         rpc->Finish(&bid_reply, &status, (void *)1);

         void *got_tag;
         bool ok = false;

         GPR_ASSERT(completion_queue.Next(&got_tag, &ok));
         GPR_ASSERT(got_tag == (void *)1);
         GPR_ASSERT(ok);

         return bid_reply;
      }

   private:
      unique_ptr<Vendor::Stub> stub;
};

BidReply stub_call(string product_name, string ip) {
   BidReply stub_reply;
   VendorClient vendor_client(grpc::CreateChannel(ip, grpc::InsecureChannelCredentials()));
   stub_reply = vendor_client.getBid(product_name);
   return stub_reply;
}

class StoreServer final {
   public:
      ~StoreServer() {
         server->Shutdown();
         completion_queue->Shutdown();
      }

      void runStore(string addr) {
         ServerBuilder builder;
         string server_addr(addr);

         builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
         builder.RegisterService(&service);
         completion_queue = builder.AddCompletionQueue();

         server = builder.BuildAndStart();
         HandleRpcs();
      }

   private:
      class CallData {
         public:
            CallData(Store::AsyncService *service, ServerCompletionQueue *completion_queue) 
               : service(service), completion_queue(completion_queue), response_writer(&context), status(CREATE) {
               proceed();
            }

            void proceed() {
               if (status == CREATE) {
                  status = PROCESS;
                  service->RequestgetProducts(&context, &product_query, &response_writer, completion_queue, completion_queue, this);
               }
               else if (status == PROCESS) {
                  new CallData (service, completion_queue);
                  string line;
                  ifstream file(vendor_addresses_file);
                  while (getline(file, line)) {
                     string product_name = product_query.product_name();
                     BidReply reply_msg = stub_call(product_name, line);
                     
                     ProductInfo product;
                     product.set_vendor_id(reply_msg.vendor_id());
                     product.set_price(reply_msg.price());
                  
                     product_reply.add_products()->CopyFrom(product);
                     product_reply.products(0).vendor_id();
                     product_reply.products(0).price();
                  }
                  status = FINISH;
                  response_writer.Finish(product_reply, Status::OK, this);
               }
               else {
                  GPR_ASSERT(status == FINISH);
                  delete this;
               }
            }

         private:
            Store::AsyncService *service;
            ServerCompletionQueue *completion_queue;
            ServerContext context;
            ProductQuery product_query;
            ProductReply product_reply;
            ServerAsyncResponseWriter<ProductReply> response_writer;
            enum CallStatus {
               CREATE,
               PROCESS,
               FINISH
            };
            CallStatus status;
      };

      void HandleRpcs() {
         new CallData(&service, completion_queue.get());
         void *tag;
         bool ok;

         while (1) {
            GPR_ASSERT(completion_queue->Next(&tag, &ok));
            GPR_ASSERT(ok);
            static_cast<CallData *>(tag)->proceed();
         }
      }

      unique_ptr<ServerCompletionQueue> completion_queue;
      Store::AsyncService service;
      unique_ptr<Server> server;
};

int run_store() {
   StoreServer StoreServer;
   StoreServer.runStore(store_address);
   return 0;
}

int main(int argc, char** argv) {
   unsigned int num_threads;
   if (argc != 4) {
      cout << "Invalid Input Parameter! Input format: ./Name vendor_addresses_file store_addressess number_of_threads" << endl;
      return EXIT_FAILURE;
   }
   if (argc >= 2) vendor_addresses_file = string(argv[1]);
   if (argc >= 3) store_address = string(argv[2]);
   if (argc == 4) num_threads = std::atoi(argv[3]);

   threadpool thread_pool(num_threads);
   thread_pool.addJob(&run_store);
   thread_pool.joinAll();

   return EXIT_SUCCESS;
}