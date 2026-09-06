#include <string>
#include <list>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <optional>
#include <cassert>

#include "OrderedMap.hpp"


enum class Type : char { Limit, Market, Peg };

enum class Side : bool { Sell, Buy };

static_assert(static_cast<std::size_t>(Side::Sell) == 0);
static_assert(static_cast<std::size_t>(Side::Buy) == 1);

enum class Status : char { Active, Partial, Total, Canceled };

enum class Reject : char {
    None,
    NotFound,
    AlreadyCanceled,
    AlreadyFilled,
    NotResting,
    NoMarketPrice,
    MissingPrice,
    UnexpectedPrice,
    InvalidQty
};

inline constexpr const char* to_string(Reject r) noexcept {
    switch (r) {
        case Reject::None:            return "ok";
        case Reject::NotFound:        return "order not found";
        case Reject::AlreadyCanceled: return "order already canceled";
        case Reject::AlreadyFilled:   return "order already totally executed";
        case Reject::NotResting:      return "market order does not rest in the book";
        case Reject::NoMarketPrice:   return "no market price - insert a limit order first";
        case Reject::MissingPrice:    return "limit order requires a price";
        case Reject::UnexpectedPrice: return "market/peg order must not carry a price";
        case Reject::InvalidQty:      return "quantity must be positive";
    }
    return "unknown";
}

inline constexpr std::size_t idx(Side s) noexcept {
    return static_cast<std::size_t>(s);
}
inline constexpr Side opposite(Side s) noexcept {
    return static_cast<Side>(idx(s) ^ 1u);
}

// ------------------------------------------------------------ OrderBook ----

class OrderBook {
    public:
        // Grouped fields due to alignment (memory efficiency).
        // 20 Bytes.
        struct Order {
            public:
                int    get_id()     const noexcept { return id; }
                int    get_qty()    const noexcept { return qty; }
                int    get_price()  const noexcept { return price; }
                Type   get_type()   const noexcept { return type; }
                Side   get_side()   const noexcept { return side; }
                Status get_status() const noexcept { return status; }

            private:
                Order(Type t, Side s, int q, int p) noexcept : 
                    type(t), side(s), qty(q), price(p) {}

                int qty, price, id, seq;
                Type type;
                Side side;
                Status status;

                friend class OrderBook;
        };

        // 20 Bytes.
        struct Trade {
            int price, qty;
            int bid_id, offer_id;
            Side aggressor; // Aggressor side.
        };

        // 8 Bytes.
        struct OpResult {
            OpResult(Reject r, int i = 0) : reject(r), id(i) {}

            int id = 0;
            Reject reject = Reject::None;
            bool check() const noexcept { return reject == Reject::None; }
        };

        // 12 Bytes.
        struct BookOut {
            int qty, price;
            Type type;
        };

    public:
        OpResult insert_order(Type type, Side side, int qty,
                                  std::optional<int> price = std::nullopt);
        OpResult cancel_order(int order_id);
        OpResult change_order(int order_id, int new_qty,
                            std::optional<int> new_price = std::nullopt);
        
        std::vector<BookOut> book_view(Side s) const;
        const Order* locate_order(int order_id) const;

        const std::vector<Trade>& pending_trades() const noexcept { return trades_; }
        void clear_trades() noexcept { trades_.clear(); }

    private:
        using OrderQueue = std::list<Order>;
        using BookMap = OrderedMap<int, OrderQueue>;
        using IndexMap = std::unordered_map<int, OrderQueue::iterator>;

        static constexpr int key_of(Side s, int price) noexcept {
            return (s == Side::Buy) ? -price : price;
        }
        static constexpr int price_of(Side s, int key) noexcept {
            return (s == Side::Buy) ? -key : key;
        }

        // 24 Bytes (in a 64-bit machine; 16 Bytes in a 32-bit).
        struct Top {
            OrderQueue* queue = nullptr;
            Order* order = nullptr;
            int price = 0;
            bool is_peg = false;
        };

        BookMap&          lim_map(Side s)         noexcept { return lim_book_[idx(s)]; }
        const BookMap&    lim_map(Side s)   const noexcept { return lim_book_[idx(s)]; }
        OrderQueue&       peg_queue(Side s)       noexcept { return peg_book_[idx(s)]; }
        const OrderQueue& peg_queue(Side s) const noexcept { return peg_book_[idx(s)]; }
        Top&              top(Side s)             noexcept { return top_[idx(s)]; }

        bool resolve_top(Side s) noexcept;
        void execute(Order& bid, Order& offer, int price);
        void retire_top(Side s);
        void run_matching();

        // The index order of map_book must be kept according to Side struct: Side::Sell = 0, Side::Buy = 1.
        // Although both BookMaps are equal in type, the buy map will be ordered in decreasing order by
        // rewriting its index with opposite (minus) sign. The order price will keep the same.
        BookMap lim_book_[2];
        OrderQueue peg_book_[2];
        OrderQueue history_;
        IndexMap ids_;
        Top top_[2];
        std::vector<Trade> trades_;

        int seq_count_ = 1; // book time.
        int id_count_ = 1; // order id counter.
};

/* time: O(1); memory: O(1). */
bool OrderBook::resolve_top(Side s) noexcept {
    BookMap& lim_map_ = lim_map(s);
    if (lim_map_.empty())
        return false;

    auto lim_book_it = lim_map_.begin(); // time: O(1); memory: O(1)
    OrderQueue& lim_queue_ = lim_book_it->second;
    OrderQueue& peg_queue_ = peg_queue(s);

    Top& t = top(s);
    t.price = price_of(s, lim_book_it->first);
    t.is_peg = !peg_queue_.empty() && peg_queue_.front().seq < lim_queue_.front().seq;
    t.queue = t.is_peg ? &peg_queue_ : &lim_queue_;
    t.order = &t.queue->front();
    return true;
}

/* time: O(1) amortized; memory: O(1) amortized. */
void OrderBook::execute(Order& bid, Order& offer, int price) {
    const int qty = std::min(bid.qty, offer.qty);
    bid.qty -= qty;
    offer.qty -= qty;
    bid.status = (bid.qty == 0) ? Status::Total : Status::Partial;
    offer.status = (offer.qty == 0) ? Status::Total : Status::Partial;
    // time: O(1) amortized; memory: O(1) amortized.
    trades_.push_back(Trade{price, qty, bid.id, offer.id,
                            (bid.seq > offer.seq) ? Side::Buy : Side::Sell});
}

void OrderBook::run_matching() {
    // time: O(1); memory: O(1) per break-condition evaluation.
    while (resolve_top(Side::Buy) && resolve_top(Side::Sell)) {
        // time: O(1); memory: O(1).
        Top& buy = top(Side::Buy);
        Top& sell = top(Side::Sell);

        if (buy.price < sell.price)
            // There is no trade when bid < offer
            break;

        const int price = (buy.order->seq < sell.order->seq) ? buy.price : sell.price;
        // time: O(1) amortized; memory: O(1) amortized.
        execute(*buy.order, *sell.order, price);

        // time: O(1), O(log n) when the level empties; memory: O(1).
        if (buy.order->qty == 0)
            retire_top(Side::Buy);
        if (sell.order->qty == 0)
            retire_top(Side::Sell);
    }
}

/* time: O(1), O(log n) when the level empties; memory: O(1). */
void OrderBook::retire_top(Side s) {
    Top& t = top(s);

    auto id_it = ids_.find(t.order->id); // time: O(1), memory: O(1).
    assert(id_it != ids_.end());
    history_.splice(history_.begin(), *t.queue, id_it->second); // time: O(1), memory: O(1).

    if (!t.is_peg && t.queue->empty())
        // If the list empties.
        lim_map(s).erase(key_of(s, t.price)); // time: O(1), memory: O(1).

    t.order = nullptr;
    t.queue  = nullptr;
}

/* time: ; memory: */
OrderBook::OpResult OrderBook::insert_order(Type type, Side side, int qty,
                                            std::optional<int> price) {
    if (qty <= 0)
        return {Reject::InvalidQty};
    if (type == Type::Limit && !price.has_value())
        return {Reject::MissingPrice};
    if (type != Type::Limit && price.has_value())
        return {Reject::UnexpectedPrice};

    Order order(type, side, qty, price.value_or(0));
    order.id = id_count_++;
    order.seq = seq_count_++;

    switch (type) {
        case Type::Peg: {
            // time: O(1); memory: O(1).
            // Insert the order to the back of the (side) peg_list book.
            peg_queue(side).push_back(order);
            // Create the index.
            ids_.emplace(order.id, std::prev(peg_queue(side).end())); // time: O(1); memory: O(1).
            break;
        }

        case Type::Limit: {
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Inserts inline the price in the (order.side) OrderMap. Returns the iterator
            // for the created list or for the existing.
            auto map_it = lim_map(side).try_emplace(key_of(side, order.price)).first;
            // Insert the order to the back of the (side) limit book.
            map_it->second.push_back(order); // time: O(1); memory: O(1)
            // Create the index.
            ids_.emplace(order.id, std::prev(map_it->second.end())); // time: O(1); memory: O(1).

            run_matching();
            break;
        }

        case Type::Market: {
            const Side opp = opposite(side);
            if (lim_map(opp).empty()) {
                order.status = Status::Canceled;
                history_.push_front(order); // time: O(1); memory: O(1).
                // Create the index;
                ids_.emplace(order.id, history_.begin()); // time: O(1); memory: O(1).

                return {Reject::NoMarketPrice, order.id};
            }

            if (side == Side::Buy)
                // time: O(1); memory: O(1) at each break-condition evaluation.
                while (order.qty > 0 && resolve_top(opp)) {
                    Top& t = top(opp);
                    execute(order, *t.order, t.price); // time: O(1) amortized; memory: O(1) amortized.
                    if (t.order->qty == 0)
                        retire_top(opp); // time: O(1), O(log n) when the level empties; memory: O(1).
                }
            else
                // time: O(1); memory: O(1) at each break-condition evaluation.
                while (order.qty > 0 && resolve_top(opp)) {
                    Top& t = top(opp);
                    execute(*t.order, order, t.price); // time: O(1) amortized; memory: O(1) amortized.
                    if (t.order->qty == 0)
                        retire_top(opp); // time: O(1), O(log n) when the level empties; memory: O(1).
                }
            order.status = (order.qty == 0)  ? Status::Total
                         : (order.qty < qty) ? Status::Partial
                                             : Status::Canceled;
            history_.push_front(order); // time: O(1); memory: O(1).
            ids_.emplace(order.id, history_.begin()); // time: O(1); memory: O(1).
            break;
        }
    }
    return {Reject::None, order.id};
}

OrderBook::OpResult OrderBook::cancel_order(int order_id) {
    auto id_it = ids_.find(order_id);
    if (id_it == ids_.end())
        return {Reject::NotFound};

    OrderQueue::iterator it_queue = id_it->second;
    if (it_queue->status == Status::Canceled)
        return Reject::AlreadyCanceled;
    if (it_queue->status == Status::Total)
        return Reject::AlreadyFilled;
    if (it_queue->type == Type::Market)
        return Reject::NotResting;

    if (it_queue->type == Type::Peg) {
        // time: O(1); memory: O(1).
        // Removes the order from the (side) peg_list book and inserts it_queue in the history.
        history_.splice(history_.begin(), peg_queue(it_queue->side), it_queue);
    }
    else {
        // O(1) time complexity due to OrderedMap structure.
        // Iterator for an map limit_map(it_queue->side) element (list).
        auto map_it = lim_map(it_queue->side).find(key_of(it_queue->side, it_queue->price)); // time: O(1); memory: O(1).
        assert(map_it != lim_map(it_queue->side).end());
        history_.splice(history_.begin(), map_it->second, it_queue); // time: O(1); memory: O(1).
        if (map_it->second.empty())
            // Remove the map node. Needed to keep easy track of bid and offer/ask.
            lim_map(it_queue->side).erase(map_it->first);
    }
    it_queue->status = Status::Canceled;

    return {Reject::None};
}

OrderBook::OpResult OrderBook::change_order(int order_id, int new_qty,
                                            std::optional<int> new_price) {
    if (new_qty <= 0)
        return Reject::InvalidQty;

    auto id_it = ids_.find(order_id);
    if (id_it == ids_.end())
        return {Reject::NotFound};

    OrderQueue::iterator it_queue = id_it->second;
    if (it_queue->status == Status::Canceled)
        return Reject::AlreadyCanceled;
    if (it_queue->status == Status::Total)
        return Reject::AlreadyFilled;
    if (it_queue->type == Type::Market)
        return Reject::NotResting;
    if (it_queue->type == Type::Peg && new_price.has_value())
        return Reject::UnexpectedPrice;

    const bool price_changed = new_price.has_value() && *new_price != it_queue->price;
    const bool loses_priority = price_changed || (new_qty > it_queue->qty);

    it_queue->qty = new_qty;
    if (loses_priority)
        it_queue->seq = seq_count_++;

    if (it_queue->type == Type::Peg) {
        if (loses_priority)
            // time: O(1); memory: O(1)
            // Moves the order to the back of the (side) peg_list book.
            peg_queue(it_queue->side).splice(peg_queue(it_queue->side).end(), peg_queue(it_queue->side), it_queue);
    }
    else {
        // time: O(1); memory: O(1).
        // O(1) time complexity due to OrderedMap structure.
        // Iterator for an map limit_map(it->side) element (list).
        auto map_it = lim_map(it_queue->side).find(key_of(it_queue->side, it_queue->price));
        assert(map_it != lim_map(it_queue->side).end());

        auto new_map_it = map_it;
        if (price_changed) {
            it_queue->price = *new_price;
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Gets the other price list.
            new_map_it = lim_map(it_queue->side).try_emplace(key_of(it_queue->side, it_queue->price)).first;
        }
        if (loses_priority)
        // time: O(1); memory: O(1).
        // Moves the order to the back of the queue.
            new_map_it->second.splice(new_map_it->second.end(), map_it->second, it_queue);
        if (price_changed) {
            if (map_it->second.empty())
                // If the list empties, the map node must be deleted.
                lim_map(it_queue->side).erase(map_it->first);

            run_matching();
        }
    }

    return {Reject::None};
}

std::vector<OrderBook::BookOut> OrderBook::book_view(Side s) const {
    std::vector<BookOut> view;
    const BookMap& lim_map_ = lim_map(s);
    const OrderQueue& peg_queue_ = peg_queue(s);

    if (lim_map_.empty())
        // Iterates over peg queue.
        // No need to check if peg_queue_ is empty; the loop breaks naturally.
        for (const Order& peg_order : peg_queue_)
            view.push_back({peg_order.qty, peg_order.price, peg_order.type});
    else {
        auto it_map = lim_map_.begin();
        auto peg_queue_it = peg_queue_.begin();

        // Iterate over first limit queue (best price queue).
        for (const Order& lim_order : it_map->second) {
            while (peg_queue_it != peg_queue_.end() && peg_queue_it->seq < lim_order.seq) {
                // While the current peg order has priority.
                // time: O(1) amortized; memory: O(1) amortized.
                view.push_back({peg_queue_it->qty, peg_queue_it->price, peg_queue_it->type});
                ++peg_queue_it;
            }
            // time: O(1) amortized; memory: O(1) amortized.
            view.push_back({lim_order.qty, lim_order.price, lim_order.type});
        }
        // Iterate over the remainer peg queue.
        for (; peg_queue_it != peg_queue_.end(); ++peg_queue_it)
            // time: O(1) amortized; memory: O(1) amortized.
            view.push_back({peg_queue_it->qty, peg_queue_it->price, peg_queue_it->type});
        // Iterate over the map, according to price order (price priority).
        for (++it_map; it_map != lim_map_.end(); ++it_map)
            // Iterate over the current limit queue.
            for (const Order& lim_order : it_map->second)
                // time: O(1) amortized; memory: O(1) amortized.
                view.push_back({lim_order.qty, lim_order.price, lim_order.type});
    }

    return view;
}

const OrderBook::Order* OrderBook::locate_order(int order_id) const {
    const auto& id_it = ids_.find(order_id);
    return (id_it == ids_.end()) ? nullptr : &*id_it->second;
}
