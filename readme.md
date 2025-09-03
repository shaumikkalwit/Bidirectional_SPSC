In our ongoing work on a **real-time control system** for a CDPR, one of the foundational challenges is handling **safe and efficient communication between threads,** particularly between a real-time thread that handles time-critical motor commands and a non-real-time thread that performs higher-level state machine changes and reads from the real-time thread.

This section walks through our **C++ implementation of a bidirectional Single-Producer Single-Consumer (SPSC) architecture**, a pattern that's used in my lab's Cable-Driven Parallel Robot’s motor control. The goal is to provide **lock-free, low-latency message passing** between two threads running at different rates, a classic but tricky problem in real-time robotics.

---

### Why SPSC? Why Bidirectional?

Typical producer-consumer queues are unidirectional; one thread sends, the other receives. But in our **real-world real-time control system**, we need **two-way communication**:

- An **observer thread** (non-real-time) sends command updates (e.g., state machine changes, shutdown signals).
- A **real-time thread** sends back data (e.g., sensor readings, control feedback) at a much higher frequency.

### Implementation Highlights
### Mailbox for Commands

We use a **double-buffered mailbox** to send commands from the non-RT thread to the RT thread. It’s not atomic on the whole `Msg` object (since it's a complex struct), but we use atomic **index swapping** to achieve safe publication.
This lets us `peek()` from the RT thread without worrying about torn reads.

### Circular Queue for Feedback
For sending data **from the RT thread to the observer**, we use a **lock-free ring buffer** with a fixed size (power of 2).
The RT thread `try_push()`s into it at a 20ms rate, and the observer `try_pop()`s every 100ms (will be 2ms rate and 10ms rate when implemented with the motor code).
All access is done with relaxed/acquire/release memory ordering, and aligned to 64-byte cache lines to avoid false sharing.
