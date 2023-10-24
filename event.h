#pragma once

// external
#include "concurrentqueue.h"

// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <concepts>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <type_traits>
#include <vector>

template <typename T>
concept Clock = std::same_as<T, std::chrono::high_resolution_clock> || std::same_as<T, std::chrono::steady_clock> || std::same_as<T, std::chrono::system_clock>;

template <Clock T = std::chrono::high_resolution_clock>
class Timer
{
private:
    std::chrono::time_point<T> start;
public:
    Timer() { reset_timer(); }

    void reset_timer() { start = T::now(); }
    float get_time() const { return std::chrono::duration_cast<std::chrono::seconds>(T::now() - start).count(); }
    std::chrono::nanoseconds get_time_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(T::now() - start); }
};

#ifdef USE_X86INTRINSICS
#include <intrin.h>
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)

/**
 * bitScanForward
 * @param bb bitboard to scan
 * @precondition bb != 0
 * @return index (0..63) of least significant one bit
 */
int bit_scan_fw(U64 x) {
   unsigned long index;
   assert (x != 0);
   _BitScanForward64(&index, x);
   return (int) index;
}

/**
 * bitScanReverse
 * @param bb bitboard to scan
 * @precondition bb != 0
 * @return index (0..63) of most significant one bit
 */
int bit_scan_rv(U64 x) {
   unsigned long index;
   assert (x != 0);
   _BitScanReverse64(&index, x);
   return (int) index;
}
#else

/**
 * bitScanForward
 * @param bb bitboard to scan
 * @precondition bb != 0
 * @return index (0..63) of least significant one bit
 */
inline int bit_scan_fw(uint64_t x) 
{
   asm ("bsfq %0, %0" : "=r" (x) : "0" (x));
   return (int) x;
}

/**
 * bitScanReverse
 * @param bb bitboard to scan
 * @precondition bb != 0
 * @return index (0..63) of most significant one bit
 */
inline int bit_scan_rv(uint64_t x) {
   asm ("bsrq %0, %0" : "=r" (x) : "0" (x));
   return (int) x;
}
#endif

class Event
{
public:
    virtual ~Event() {}
    virtual constexpr size_t get_id() = 0;
};

template <typename T> 
concept EventDerived = std::is_base_of_v<Event, T>;

template <typename T>
concept HasId = requires(T) 
{
    { T::id };
};

template <typename T>
concept ValidEvent = EventDerived<T> && HasId<T>;

template <typename T, ValidEvent E>
using handler_fun_t = void (T::*)(E*);

template <typename T> 
class DeletePointerView 
{
private:
    T* pointer;
public:
    DeletePointerView(T* input) : pointer(input) {}    

    DeletePointerView() : pointer(nullptr) {}
    DeletePointerView(const DeletePointerView<T>& other) = delete;
    DeletePointerView& operator=(const DeletePointerView<T>& other) = delete;

    DeletePointerView(DeletePointerView<T>&& other)
    {
        this->pointer = other.pointer; 
        other.pointer = nullptr;
    }

    DeletePointerView& operator=(DeletePointerView<T>&& other)
    {
        this->pointer = other.pointer; 
        other.pointer = nullptr;
        return *this;
    };

    T* data() const { return pointer; }

    ~DeletePointerView()
    {
        delete pointer;
    }
};

#define EVENT_GEN(x)

#ifdef EVENT_IMPLEMENTATION

template <unsigned int N>
struct reader 
{
    friend auto counted_flag(reader);
};

template <unsigned int N>
struct setter 
{
    friend auto counted_flag(reader<N>) {}
    static constexpr unsigned n = N;
};

template <
    auto Tag,
    unsigned NextVal = 0
>
consteval auto counter_impl() 
{
    constexpr bool counted_past_value = requires(reader<NextVal> r) 
    {
        counted_flag(r);
    };

    if constexpr (counted_past_value) 
    {
        return counter_impl<Tag, NextVal + 1>();
    }
    else 
    {
        return setter<NextVal>::n;
    }
}

template <
    auto Tag = []{},
    auto Val = counter_impl<Tag>()
>
constexpr auto counter = Val;

#undef EVENT_GEN
#define EVENT_GEN(x) const size_t x::id = counter<>;\
constexpr size_t x::get_id() { return x::id; }\

#endif

extern const size_t max_event_types;

class IEventHandler 
{ 
public: 
	virtual ~IEventHandler() {}; 
	virtual void exec(Event* event) = 0; 
    virtual void* get_handler_ptr() = 0;
}; 

template <typename T, ValidEvent E> 
class EventHandler : public IEventHandler 
{ 
private:
    handler_fun_t<T, E> mem_fun;
    T* instance;

public:
	EventHandler(T* instance, handler_fun_t<T, E> mem_fun) 
        : instance(instance), mem_fun(mem_fun) {}; 

    ~EventHandler() override {}

    void exec(Event* event) override
    {
        (instance->*mem_fun)(static_cast<E*>(event));
    }

    void* get_handler_ptr() override
    {
        return (void*) instance;
    }
};

class EventProcessor 
{
private:
    moodycamel::ProducerToken token; 
    std::queue<std::shared_ptr<DeletePointerView<Event>[]>> events_queue;
    std::queue<size_t> events_size_queue;
    std::vector<std::vector<IEventHandler*>> handlers;  
public:
    EventProcessor(moodycamel::ProducerToken&& token)
        : token(std::move(token)), handlers(max_event_types) {}

    EventProcessor(const EventProcessor& other) = delete;
    EventProcessor& operator=(const EventProcessor& other) = delete;

    EventProcessor(EventProcessor&& other)
        : token(std::move(other.token)), 
        events_queue(std::move(other.events_queue)), 
        events_size_queue(std::move(other.events_size_queue)),
        handlers(std::move(other.handlers)) {}

    EventProcessor& operator=(EventProcessor&& other)
    {
        token = std::move(other.token);
        events_queue = std::move(other.events_queue);
        events_size_queue = std::move(other.events_size_queue);
        handlers = std::move(other.handlers);
        return *this;
    }

    ~EventProcessor()
    {
        for (auto& handler : handlers)
        {
            for (auto& x : handler)
            {
                delete x;
            }
        }
    }
     
    // Has a producer token for adding events
    inline moodycamel::ProducerToken& get_producer() { return token; }
 
    // Managing event handlers
    template <typename T, ValidEvent E> 
    inline void subscribe(T* handler, handler_fun_t<T, E> handler_fun)
    {
        size_t e = E::id;
        handlers[E::id].emplace_back(
            (IEventHandler*) new EventHandler<T, E>(handler, handler_fun));
    }

    template <typename T> 
    inline void unsubscribe(T* handler)
    {
        for (auto grouped_handlers : handlers)
        {
            std::erase_if(grouped_handlers, 
                [handler](IEventHandler* x){
                    if (handler == x->get_handler_ptr()) delete x;
                    return handler == x->get_handler_ptr();
            });
        }
    }

    // Processing events
    void process_events();
    void add_events(const std::shared_ptr<DeletePointerView<Event>[]>& events, 
        size_t num_events);
};

#ifdef EVENT_IMPLEMENTATION

void EventProcessor::process_events()
{
    for (; !events_queue.empty(); )
    {
        auto& events = events_queue.back();
        size_t events_size = events_size_queue.back();
        
        for (size_t i = 0; i < events_size; i++)
        {
            Event* event = events[i].data(); 
            for (auto& handler : handlers[event->get_id()])
            {
                handler->exec(event);
            }
        }

        events_queue.pop();
        events_size_queue.pop();
    }
}

void EventProcessor::add_events(
    const std::shared_ptr<DeletePointerView<Event>[]>& events, size_t num_events)
{
    events_queue.push(events); 
    events_size_queue.push(num_events);
}

#endif

// Nocall are methods that cannot be called at the same time as the method being called
class MultiEventManager
{
private:
    // The number of created processor 
    // Recyclable processors? using queue 
    size_t subtracted = 0;
    std::atomic<size_t> event_count = 0;
    std::vector<EventProcessor> processors;

    // Event queue
    moodycamel::ConcurrentQueue<std::pair<Event*, size_t>> event_queue;

    // Processor and subscribe mutex
    std::shared_mutex processor_and_sub;
public:
    // Creates a new processor which can be used to run processes on different threads (ie processes that use processors are thread safe)
    size_t get_processor(); 
    
    // Adds a handler to a processor 
    template <typename T, EventDerived E>
    inline void subscribe(size_t processor_id, T* handler, 
        handler_fun_t<T, E> handler_fun)
    {
        std::shared_lock lock(processor_and_sub);
        processors[processor_id].subscribe(handler, handler_fun);
    }

    // Removes a handler from a processor
    template <typename T>
    inline void unsubscribe(size_t processor_id, T* handler)
    {   
        std::shared_lock lock(processor_and_sub);
        processors[processor_id].unsubscribe(handler);
    }

    // Add an event that will be processed by every processor
    // Should be destroyed after it is processed by every processor 
    // This is the processor it is being submitted by, not the processor it will appear on (it will appear on all processors)
    // Very thread safe (can be used with any method besides get_processor)
    void submit(size_t processor_id, Event* event);

    // Process every event using a certain processor
    // Thread safe only with submit
    void process_events(size_t processor_id);

    // Takes events and moves them onto the processors so they can efficiently process events and properly delete them 
    // Thread safe only with submit 
    void move_to_processors();
};

// Count sort in base N
template <typename T, size_t N> 
void count_sort(std::vector<std::pair<T, size_t>>& input, 
    std::vector<std::pair<T, size_t>>& output, size_t iterations)
{
    size_t count[N] = {0};
    size_t exp = std::pow(N, iterations); 
    
    for (size_t i = 0; i < input.size(); i++) 
    {
        count[(input[i].second / exp) % N]++;
    }

    for (size_t i = 1; i < N; i++) 
        count[i] += count[i - 1];

    for (size_t i = input.size(); i > 0; i--)
    {
        output[count[(input[i - 1].second / exp) % N] - 1] = input[i - 1];
        count[(input[i - 1].second / exp) % N]--;
    }
}

// Radix with base N
template <typename T, size_t N> 
void radix(std::vector<std::pair<T, size_t>>& input)
{
    std::vector<std::pair<T, size_t>> s_input(input.size());
    size_t iterations = 0;
    size_t max_num = max_val(input);

    while (max_num / (size_t) std::pow(N, iterations))
    {
        count_sort<T, N>(iterations % 2 == 0 ? input : s_input, 
            iterations % 2 == 0 ? s_input : input, iterations);   
        iterations++;
    }

    if (iterations % 2) input = s_input;
}

template <typename T> 
size_t max_val(const std::vector<std::pair<T, size_t>>& input)
{
    size_t max_val = 0;
    for (auto& v : input)
    {
        max_val = max_val < v.second ? v.second : max_val;
    }
    return max_val;
}

consteval size_t compile_pow(size_t base, size_t exp) 
{ 
    size_t rval = base;
    for (size_t i = 0; i < exp; i++) rval *= base; 
    return rval; 
}

// Multithreaded radix, splits the numbers into buckets and then calls radix on each bucket with base N
template <typename T, size_t N, size_t base = 4>
std::vector<T> multithreaded_radix(
    std::vector<std::pair<T, size_t>>& input)
{
    size_t max_num = max_val(input);

    if (max_num == 0)
    {
        std::vector<T> output;
        for (auto& val : input)
        {
            output.push_back(val.first); 
        }
        
        return output;
    }
    
    // Each bucket for each thread
    std::array<
        std::vector<std::pair<Event*, size_t>>, compile_pow(2, base)> buckets;

    for (size_t i = 0; i < input.size(); i++)
    {
        buckets[input[i].second >> 
            (bit_scan_rv(max_num) + 1 - base)].push_back(input[i]);
    }

    Timer timer;

    std::array<std::thread, compile_pow(2, base)> threads;
    for (size_t i = 0; i < threads.size(); i++)
    {
        threads[i] = std::thread(radix<T, N>, std::ref(buckets[i]));
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    std::vector<T> output;

    for (auto& bucket : buckets)
    {
        for (auto& val : bucket)
        {
            output.push_back(val.first);
        }
    }

    return output;
}

std::shared_ptr<DeletePointerView<Event>[]> create_delete_shared(
    const std::vector<Event*>& input)
{
    auto shared_ptr = new DeletePointerView<Event>[input.size()];
    auto output = std::shared_ptr<DeletePointerView<Event>[]>(shared_ptr);
    memcpy((void*) shared_ptr, (void*) input.data(), 
        input.size() * sizeof(DeletePointerView<Event>)); 

    return output;
}

#ifdef EVENT_IMPLEMENTATION

size_t MultiEventManager::get_processor()
{
    std::unique_lock lock(processor_and_sub);
    processors.push_back(
        std::move(EventProcessor(moodycamel::ProducerToken(event_queue))));
    return processors.size() - 1;
}

void MultiEventManager::submit(size_t processor_id, Event* event)
{
    event_queue.enqueue(processors[processor_id].get_producer(), 
        std::make_pair(event, event_count.fetch_add(1)));
}

void MultiEventManager::process_events(size_t processor_id)
{
    processors[processor_id].process_events();
}

void MultiEventManager::move_to_processors()
{
    size_t event_count = this->event_count;

    std::vector<std::pair<Event*, size_t>> stored;
    stored.resize(event_count - subtracted);
    size_t event_got = event_queue.try_dequeue_bulk(stored.data(), 
        event_count - subtracted); 

    stored.resize(event_got);

    auto copy_to = multithreaded_radix<Event*, 32>(stored);
    auto event_shared = create_delete_shared(copy_to);

    for (auto& processor : processors)
    {
        processor.add_events(event_shared, event_got);
    }
    
    subtracted += event_got;
}

#endif

#define MAX_EVENT_INIT const size_t max_event_types = counter<>;
