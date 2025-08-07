#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <atomic>

/**
 * @brief A generic message structure for communication between threads.
 *
 * This simple "Plain Old Data" (POD) struct is used for both sending commands
 * from the Observer to the RT thread and for sending data back from the RT
 * thread to the Observer
 */
struct Message {
    float arrayOfNumbers[8];
    bool keepRunning;
};

// This is a compile-time check that ensures the Message struct is "trivially copyable".
// This is critical for high-performance applications because it guarantees that
// copying a Message can be done with a simple, fast, bit-for-bit memory copy (like memcpy),
// without any unexpected side effects from user-defined constructors or destructors.
static_assert(std::is_trivially_copyable_v<Message>,"msg must be trivial");

struct Mailbox {
    // Two slots are used for double-buffering. This allows the producer
    // to write to one slot while the consumer safely reads from the other,
    // preventing data corruption (torn reads).
    Message slots[2];

    // An atomic index is used as a controller because std::atomic is not
    // guaranteed to work on a complex struct like Msg. This index points
    // to the slot containing the latest, complete data.
    alignas(64) std::atomic<int> latest_idx{0};
};

/**
 * @brief A lock-free SPSC queue for the RT -> Observer data channel.
 *
 * This struct implements one half of a bidirectional SPSC communication
 * system. It serves as the channel for the RT thread to
 * send a stream of data messages for the Observer thread to read from. The other direction,
 * for sending commands, is handled by the Mailbox.
 */
struct alignas(64) Ring {
    // alignas(64) prevents "false sharing" by ensuring this struct starts on a
    // new cache line, a 64-byte boundary in memory.

    /// The write index, modified only by the producer (the RT thread).
    std::atomic<size_t> head{0};

    /// The read index, modified only by the consumer (the Observer thread).
    std::atomic<size_t> tail{0};

    /// The underlying circular buffer that stores the messages.
    /// Its size must be a power of two for the fast bitwise-AND indexing to work.
    Message buf[8];
};

/**
 * @brief Sends a command from the Observer thread to the RT thread
 *
 * This function is called by the low-frequency Observer thread to update the
 * command state for the RT thread. It uses a double-buffer mailbox to ensure
 * the command is sent safely without blocking and without data corruption
 *
 * @param mailbox The Mailbox to send the command to
 * @param command The Message object containing the command data
 */
void send_command(Mailbox &mailbox, const Message &command) {
    // Find the inactive slot to write to. 'relaxed' is fine because we don't
    // need to synchronize other memory with this read, just get the index value.
    const int current_idx = mailbox.latest_idx.load(std::memory_order_relaxed);
    const int write_idx = 1 - current_idx;

    // Write the new data into the hidden "staging" slot.
    mailbox.slots[write_idx] = command;

    // Atomically publish the new data. The `release` fence ensures the write
    // above is 100% complete before any other thread sees the new index.
    // This is the key to preventing torn reads.
    mailbox.latest_idx.store(write_idx, std::memory_order_release);
}

/**
 * @brief Safely peeks at the latest message in the mailbox
 * @param mailbox The mailbox to peek from
 * @return A copy of the latest, complete message
 */
Message peek(Mailbox &mailbox) {
    // Atomically load the index. The 'acquire' memory order pairs with the
    // 'release' in the send function. This creates a "happens-before"
    // relationship, guaranteeing that we only see the new index *after* the
    // message write is 100% complete
    const int read_idx = mailbox.latest_idx.load(std::memory_order_acquire);

    // Return a copy of the data from the slot that is now safe to read
    return mailbox.slots[read_idx];
}

/**
 * @brief Tries to push a data message from the RT thread into the queue
 *
 * This function is called by the high-frequency RT thread to send data back
 * to the Observer thread. It is non-blocking; if the queue is full, it will
 * immediately return false, dropping the message
 *
 * @param queue The queue to push the message into
 * @param message The Message object containing the data to be pushed
 * @return true if the message was successfully pushed, false if the queue was full
 */
bool try_push(Ring &queue, const Message &message) {
    size_t h = queue.head.load(std::memory_order_relaxed);
    size_t t = queue.tail.load(std::memory_order_acquire); 
    if (h-t == std::size(queue.buf)) // full 
        return false;

    queue.buf[h & 7] = message; 
    queue.head.store(h+1, std::memory_order_release); 
    return true;
}

/**
 * @brief Tries to pop a data message from the queue for the Observer thread.
 *
 * This function is called by the low-frequency Observer thread to read data
 * sent by the RT thread. It is non-blocking; if the queue is empty, it will
 * immediately return false
 *
 * @param queue The queue to pop the message from
 * @param[out] out The Message object where the popped data will be stored
 * @return true if a message was successfully popped, false if the queue was empty
 */
bool try_pop(Ring &queue, Message &out){
    size_t t = queue.tail.load(std::memory_order_relaxed); 
    size_t h = queue.head.load(std::memory_order_acquire);
    if (t==h){ // empty
        return false;
    }

    out = queue.buf[t & 7];
    queue.tail.store(t+1, std::memory_order_release);
    return true;
}

/**
 * @brief The main function for the high-frequency Real-Time (RT) thread.
 *
 * This function runs in a continuous loop at a fixed rate (20ms). In each
 * cycle, it peeks at the CommandMailbox to get the latest command from the
 * Observer thread. It then uses that command to generate a new data message,
 * which it pushes into the outgoing Ring queue.
 *
 * @param tx The Ring queue to push outgoing data messages into.
 * @param mailbox The Mailbox to peek for incoming commands from.
 */
void continuousThreadFunction(Ring &tx, Mailbox &mailbox){
    int i= 0;
    auto wake_up = std::chrono::high_resolution_clock::now();

    while(true) {
        wake_up += std::chrono::milliseconds(20);
        i+=1;

        Message command = peek(mailbox);

        if (!command.keepRunning) {
            break;
        }

        Message message = {};
        message.keepRunning = true;
        message.arrayOfNumbers[0] = command.arrayOfNumbers[0] + static_cast<float>(i);

        try_push(tx, message);
        printf("  RT Thread Pushed:  %f\n", message.arrayOfNumbers[0]);
        std::this_thread::sleep_until(wake_up);
    }
}

/**
 * @brief The main entry point of the program, acting as the low-frequency Observer thread.
 *
 * This function initializes the communication
 * channels, launches the high-frequency RT thread, and then enters a loop where it
 * simulates the work of an observer, sending new commands to the RT thread and
 * periodically draining the data queue to process the results
 */
int main() {
    printf("hello world\n");

    // These are what actually hold the data that are being read and written to
    Ring rtToMain;
    Mailbox mainToRT;

    Message command = {};
    command.keepRunning = true;
    command.arrayOfNumbers[0] = 0.0f;
    send_command(mainToRT, command);

    std::thread t(continuousThreadFunction, std::ref(rtToMain), std::ref(mainToRT));
    auto wake_up = std::chrono::high_resolution_clock::now();

    // Loop a few times, sending a new command each time
    for (int i = 1; i <= 4; ++i) {
        wake_up += std::chrono::milliseconds(100);
        printf("\n--- Observer Loop %d ---\n", i);

        // Set a new command value to send
        command.arrayOfNumbers[0] = static_cast<float>(i * 100);
        printf("Observer sending new command: %f\n", command.arrayOfNumbers[0]);
        send_command(mainToRT, command);

        // Wait a second to let the RT thread run
        std::this_thread::sleep_until(wake_up);

        // Now drain the rt queue to see what the RT thread produced
        Message message;
        printf("Observer reading from RT queue:\n");
        while (try_pop(rtToMain, message)) {
            printf("  > Popped RT values: %f\n", message.arrayOfNumbers[0]);
        }
    }

    // Tells real-time thread to shut down
    printf("\nObserver sending shutdown command...\n");
    command.keepRunning = false;
    send_command(mainToRT, command);

    // Wait for the thread to finish
    t.join();
    printf("done \n");

    return 0;
}