# Store with Client and Vendors using gRPC

## Introduction

This project was done by Hemang Dash solely.

This project implements major chunks of a distributed service using gRPC. We build a store which receives requests from different users, queryimg the prices offered by the different registered vendors. On each product query, the server is supposed to request all of the vendor severs provided for their bid on the queried product. Once the store has responses from all the vendors, it collates the bid and vendor_id from the vendors and sends it back to the requesting client.

We establish asynchronous gRPC communication between the store and the user client, and the store and the vendors. Upon receiving a client request, the
store assigns a thread from the thread poll to the incoming request for processing.
- The thread makes async RPC calls to the vendors.
- The thread awaits for all the results to come back.
- The thread collates the results.
- The thread replies to the store client with the results of the call.
- Having completed the execution, the thread returns to the thread pool.


## `threadpool.h`: Implementation of threadpool management

The threadpool header file contains the class `threadpool`. It contains a vector of threads called `thread_pool`, two mutexes `queue_mutex` and `wait_mutex`, a list of functions `job_queue`, an atomic integer `remaining`, two flags `hold_flag` and `finish_flag` and two condition variables `is_available` and is_await.

We have a private `task()` function which consists of a loop. While the `hold_flag` is not held, we call `next_task()`, decrement remaining and unblocks one of the waiting threads by calling `notify_one()` on `is_await`.

The private `next_task()` function waits for the condition variable `is_available`. If the `hold_flag` is true, the current task is a new task and remaining is incremented. Otherwise, the current task is the one in front of the `job_queue` (which is popped).

The public `addJob()` function first locks the `queue_mutex`. Then we add the job at the back of the `job_queue`. We increment remaining and call `notify_one()` on `is_available` to unblock a thread waiting on `is_available`.

The public `allWait()` function checks if remaining is not equal to 0, then locks the `wait_mutex`. It makes the `is_await` condition variable wait for remaining to become 0. Then it unlocks the `wait_mutex`.

The public `joinAll()` function is used to join all the threads upon completion. If the `finish_flag` is false, we call `allWait()`. We set the `hold_flag` to be true. Then we unblock whichever thread is waiting on `is_available` by calling `notify_all()` on it. For each thread in the `thread_pool`, if the thread is joinable, we join the thread. Finally we set the `finish_flag` to be true.

## `store.cc`: Implementation of store management

First, we create the Vendor Stub using a class called `VendorClient`. This class contains the stub, which is our view of the server's exposed services. This class also contains a public function called `getBid()` which assembles the client's payload (product query), sends it to the server and presents the response back from the server. To do so, we make use of `bid_query` which is the data we are sending to the server and `bid_reply` which is the container for the data we expect from the server. We define the context for the client which is used to convey extra information to the server and/or tweak certain RPC behaviors. We use a `completion_queue` to store the status of the RPC upon completion. `stub->AsyncgetProductBid()` creates an RPC object, returning an instance to the sore in "call". But, it does not actually start the RPC. Since we use the asynchronous API, we must hold on to the "call" instance to get updates on the ongoing RPC. We then request for the `bid_reply` to be updated with the server's response upon completion of the RPC. The status is updated with the indication of whether the operation was successful. We tag the request with integer 1. We block until the next result is available in the `completion_queue`. The return value of `completion_queue.Next()` is checked using `GPR_ASSERT()` and it tells us whether there is any kind of event or the `completion_queue` is shutting down. We then verify that the result from the `completion_queue` corresponds to our previous request by its tag. Finally, we check if the request was completed successfully. We return the `bid_reply`.

We have a global function `stub_call()` which instantiates the client. It requires a channel, out of which the actual RPC's are created. This channel models a connection to an endpoint. We indicate that the channel is not autheticated using `InsecureChannelCredentials()`.

We then create the `StoreServer` class. We have a function called `runStore()`. It listens on the given address without any authentication mechanism. Then we register service as the instance through which we will communicate with clients. In this case, it corresponds to an asynchronous service. We get hold of the `completion_queue` used for the asynchronous communication with the gRPC runtime. Finally, we assemble the server and proceed to the server's main loop using `HandleRpcs()`.

Within the `StoreServer` class, we have a class `CallData` encompassing the state and logic needed to serve a request. We use `completition_queue` as the
producer-consumer queue for asynchronous server notifications. We keep track of the ServerContext using context, allowing to tweak aspects of it such as the use of compression, authentication and send metadata back to the client. We use the `ProdutQuery` variable request to store what we get from the client. We use `response_writer` as the means to get back to the client. We have a state machine with the states `CREATE`, `PROCESS` and `FINISH`. The current serving state is saved in `status`. `CallData`'s constructor takes in the service instance (asynchronous server) and the completion_queue used for asynchronous communication with the gRPC runtime. It also invokes the serving logic right away. In the `proceed()` function, we make this instance progress to the `PROCESS` state. As part of the initial `CREATE` state, we request that the system starts to process `SayHello` requests. In this request, "this" acts as the tag uniquely identifying the request so that different `CallData` instances can serve different requests concurrently, in this case, the memory address of this `CallData` instance. If the status is `PROCESS`, we spawn a new `CallData` instance to serve new clients while
we process the one for this `CallData`. The instance will deallocate itself as part of its `FINISH` state. We get the vendor's reply to feed it into the
ProductReply variable reply. We let the gRPC runtime know we have finished using the memory address of this instance as the uniquely identifying tag for the event. Once in the `FINISH` state, we deallocate this instance of `CallData`.

`HandleRpcs()` spawns a new CallData instance to serve new clients. It uniquely identifies a request using the tag variable. We have an infinite loop block waiting to read the next event from the `completion_queue`. The event is uniquely identified by its tag, which in this case is the memory address of a `CallData` instance. The return value of `completion_queue.Next()` is checked using `GPR_ASSERT()` and it tells us whether there is any kind of event or the `completion_queue` is shutting down.

Our main function checks the input parameters. It takes in the vendor addresses fle, the store IP address and the number of threads.
