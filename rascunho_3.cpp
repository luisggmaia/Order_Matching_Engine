#include <list>
#include <vector>
#include <iostream>
#include "OrderedMap.hpp"


// enum class Type

enum class Type : char { Limit, Market, Peg };

// enum class Side

enum class Side : bool { Sell, Buy };

static_assert(static_cast<std::size_t>(Side::Sell) == 0);
static_assert(static_cast<std::size_t>(Side::Buy)  == 1);

static constexpr std::size_t idx(Side s) noexcept {
    return static_cast<std::size_t>(s);
}

static constexpr Side opposite(Side s) noexcept {
    return static_cast<Side>(idx(s) ^1u);
}

// enum class Status

enum class Status : char { Active, Partial, Total, Canceled };

// struct Order

struct Order {
    Order(Type t, Side s, int q, int p = 0)
        : type(t), side(s), qty(q), price(p) {};

    Type type;
    Side side;
    int qty, price;

    private:
        int id;
        Status status;

        friend class OrderBook;
};

struct Trade {
    int qty, price;
    int bid_order_id, offer_order_id;
    Side agressor;
};

// class OrderBook

class OrderBook {
    private:
        struct PriceCmp {
            bool desc;
            bool operator()(int a, int b) const noexcept { return desc ? (a > b) : (a < b); }
        };

        using OrderList = std::list<Order>;
        using BookMap = OrderedMap<int, OrderList, PriceCmp>;

        // The index order of map_book must be kept according to Side struct: Side::Sell = 0, Side::Buy = 1.
        BookMap map_book[2] = { BookMap{PriceCmp{false}}, BookMap{PriceCmp{true}} };
        OrderList peg_book[2];
        OrderList history;
        std::unordered_map<int, OrderList::iterator> ids;

        int time = 1; // order limit_book time

        BookMap& limit_map(Side s) noexcept { return map_book[static_cast<std::size_t>(s)]; }
        OrderList& peg_list(Side s) noexcept { return peg_book[static_cast<std::size_t>(s)]; }

    public:
        OrderBook();
        int insert_order(Order order);
        bool cancel_order(int order_id);
        bool change_order(int order_id, int qty, int price = 0);
        void print_book();

        const std::vector<Trade>& last_trades() const noexcept { return trades_; }

    private:
        // Buffers
        struct SideState {
            BookMap::iterator book_it;
            bool is_peg;
            Order top_order;
        };
        SideState side_[2];

        SideState& state(Side s) noexcept { return side_[idx(s)]; }
        const SideState& state(Side s) const noexcept { return side_[idx(s)]; }
        // BookMap::iterator bid_book_it_, offer_book_it_;
        // bool peg_bid_, peg_offer_;
        // OrderList *bid_ptr_, *offer_ptr_;
        // OrderList::iterator bid_list_it_, offer_list_it_;
        // Order bid_order_, offer_order_;
        // Order opp_order_;
        std::vector<Trade> trades_;
        Trade trade_;

        void update_book();
        void match(Order& bid_order, Order& offer_order);
        bool set_match_order(Side s) noexcept {
            auto& book = limit_map(s);
            if (book.empty())
                return false;
            auto& st = state(s);
            auto& pegs = peg_list(s);

            st.book_it = book.begin();
            st.is_peg = !pegs.empty() && pegs.front().id < st.book_it->second.front().id;
            st.top_order = st.is_peg ? pegs.front() : st.book_it->second.front();
            return true;
        }

};

int OrderBook::insert_order(Order order) {
    order.id = time;
    order.status = Status::Active;
    switch (order.type) {
        case Type::Peg: {
            // time: O(1); memory: O(1).
            // Insert the order to the back of the (side) peg_list book.
            peg_list(order.side).push_back(order);
            // Create the index.
            ids[order.id] = std::prev(peg_list(order.side).end());
            break;
        }
        case Type::Market: {
            // The market order demands the opposite side book to be not empty - cause its price is based on the best price of the other side.
            if (!set_match_order(opposite(order.side))) {
                std::cerr << "Error: no market price.\n";
                return 0;
            }
            order.price = limit_map(order.side).begin()->first;

            (order.side == Side::Buy) ? match(order, state(Side::Sell).top_order) : match(state(Side::Buy).top_order, order);
            if (order.qty > 0)
                order.type = Type::Limit;
            // The lack of break is intentional. Because the order becomes Limit and needs to be added to the limit book.
        }
        case Type::Limit: {
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Inserts inline the price in the (order.side) OrderMap. Returns the iterator for the created list or for the existing.
            auto it_map = limit_map(order.side).try_emplace(order.price).first;
            // time: O(1); memory: O(1)
            // Insert the order to the back of the (side) limit book.
            it_map->second.push_back(order);
            // Create the index.
            ids[order.id] = std::prev(it_map->second.end());
            if ((order.side == Side::Buy && order.price >= limit_map(Side::Sell).begin()->first) || (order.side == Side::Sell && order.price <= limit_map(Side::Buy).begin()->first))
                update_book();
            break;
        }
    }
    time++;

    return order.id;
}

bool OrderBook::cancel_order(int order_id) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end()) {
        std::cerr << "Error: order not found.\n";
        return false;
    }
    OrderList::iterator it = it_id->second;

    if (it->type == Type::Peg)
        // time: O(1); memory: O(1).
        // Removes the order from the (side) peg_list book and inserts it in the history.
        history.splice(history.begin(), peg_list(it->side), it);
    else {
        // time: O(1); memory: O(1).
        // O(1) time complexity due to OrderedMap structure.
        // Iterator for an map limit_map(it->side) element (list).
        auto it_map = limit_map(it->side).find(it->price);

        // time: O(1); memory: O(1).
        history.splice(history.begin(), it_map->second, it);
        if (it_map->second.empty())
            // Remove the map node. Needed to keep easy location of bid and ask.
            limit_map(it->side).erase(it_map->first);
    }
    // No need to advance the time.
    it->status = Status::Canceled;

    return true;
}

bool OrderBook::change_order(int order_id, int qty, int price = 0) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end()) {
        std::cerr << "Error: order not found.\n";
        return false;
    }
    OrderList::iterator it = it_id->second;
    
    if (it->type == Type::Peg) {
        it->qty = qty;
        // time: O(1); memory: O(1)
        // Moves the order to the back of the (side) peg_list book.
        peg_list(it->side).splice(peg_list(it->side).end(), peg_list(it->side), it);
    }
    else {
        // time: O(1); memory: O(1).
        // O(1) time complexity due to OrderedMap structure
        // Iterator for an map limit_map(it->side) element (list).
        auto it_map = limit_map(it->side).find(it->price);
        it->qty = qty;

        auto new_it_map = it_map;
        if (price != it->price) {
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Gets the other price list.
            new_it_map = limit_map(it->side).try_emplace(price).first;
            if (it_map->second.empty())
                // If the list empties, the map node must be deleted.
                limit_map(it->side).erase(it_map->first);
        }
        // Moves the order to the back.
        new_it_map->second.splice(new_it_map->second.end(), it_map->second, it);
    }
    // No need to advance the time.

    return true;
}

void OrderBook::update_book() {
    while (limit_map(Side::Buy).empty() or limit_map(Side::Sell).empty()) {
        bid_book_it_ = limit_map(Side::Buy).begin();
        offer_book_it_ = limit_map(Side::Sell).begin();
        if (bid_book_it_->first < offer_book_it_->first)
            // There is no trade when bid < offer
            break;

        // Evaluate whether the first pegged order has priority to the first limit order (according to the time/id)
        peg_bid_ = !peg_list(Side::Buy).empty()
                && (peg_list(Side::Buy).front().id < bid_book_it_->second.front().id);
        peg_offer_ = !peg_list(Side::Sell).empty()
                && (peg_list(Side::Sell).front().id < offer_book_it_->second.front().id);

        // Chooses the priority according to the time
        bid_order_ = (peg_bid_) ? peg_list(Side::Buy).front() : bid_book_it_->second.front();
        offer_order_ = (peg_offer_) ? peg_list(Side::Sell).front() : offer_book_it_->second.front();

        match(bid_order_, offer_order_);
    }
}

void OrderBook::match(Order& bid_order, Order& offer_order) {
    trade_.bid_order_id = bid_order.id;
    trade_.offer_order_id = offer_order.id;

    // Evaluate whether the buy qty is smaller, greater than or equal to the sell qty
    if (bid_order.qty < offer_order.qty) {
        // Bid's qty is smaller than offer's. Its trade is uncomplete.
        // Updates the qty for the offer order.
        offer_order.qty -= bid_order.qty;
        trade_.qty = bid_order.qty;
        // Requires c++20.
        // If the order is inserted in the book, then moves it to the history.
        if (ids.contains(bid_order.id)) {
            // If this order is in the book, it is certainly in the bid_book_it_ (best bid list).
            history.splice(history.begin(), bid_book_it_->second, ids[bid_order.id]);
            if (!peg_bid_ && bid_book_it_->second.empty())
                // If the list empties
                limit_map(Side::Buy).erase(bid_book_it_->first);
        }
    }
    else if (bid_order.qty > offer_order.qty) {
        // Offer's qty is smaller than bid's. Its trade is uncomplete.
        // Updates the qty for the bid order.
        bid_order.qty -= offer_order.qty;
        trade_.qty = offer_order.qty;
        // Requires c++20.
        // If the order is inserted in the book, then moves it to the history.
        if (ids.contains(offer_order.id)) {
            // If this order is in the book, it is certainly in the offer_book_it_ (best offer list).
            history.splice(history.begin(), offer_book_it_->second, ids[offer_order.id]);
            if (!peg_offer_ && offer_book_it_->second.empty())
                // If the list empties
                limit_map(Side::Sell).erase(offer_book_it_->first);
        }
    }
    else {
        trade_.qty = bid_order.qty; // = offer_order.qty;
        if (ids.contains(bid_order.id)) {
            history.splice(history.begin(), bid_book_it_->second, ids[bid_order.id]);
            if (!peg_bid_ && bid_book_it_->second.empty())
                limit_map(Side::Buy).erase(bid_book_it_->first);
        }
        if (ids.contains(offer_order.id)) {
            history.splice(history.begin(), offer_book_it_->second, ids[offer_order.id]);
            if (!peg_offer_ && offer_book_it_->second.empty())
                limit_map(Side::Sell).erase(offer_book_it_->first);
        }
    }

    // The executed price is determined by the prioritary order.
    trade_.price = (trade_.bid_order_id < trade_.offer_order_id) ? bid_order.price : offer_order.price;

    trades_.push_back(trade_);
}

void OrderBook::print_book() {

}

/*
void OrderBook::update_book() {
    while (true) {
        if (limit_map(Side::Buy).empty() or limit_map(Side::Sell).empty())
            break;

        bid_book_it_ = limit_map(Side::Buy).begin();
        offer_book_it_ = limit_map(Side::Sell).begin();
        if (bid_book_it_->first < offer_book_it_->first)
            // There is no trade when bid < offer
            break;

        // Evaluate whether the first pegged order has priority to the first limit order (according to the time/id)
        peg_bid_ = !peg_list(Side::Buy).empty()
                && (peg_list(Side::Buy).front().id < bid_book_it_->second.front().id);
        peg_offer_ = !peg_list(Side::Sell).empty()
                && (peg_list(Side::Sell).front().id < offer_book_it_->second.front().id);

        // Chooses the priority according to the time
        bid_list_it_ = (peg_bid_) ? peg_list(Side::Buy).begin() : bid_book_it_->second.begin();
        offer_list_it_ = (peg_offer_) ? peg_list(Side::Sell).begin() : offer_book_it_->second.begin();

        trade_.bid_order_id = bid_list_it_->id;
        trade_.offer_order_id = offer_list_it_->id;

        // Evaluate whether the buy qty is smaller, greater than or equal to the sell qty
        if (bid_list_it_->qty < offer_list_it_->qty) {
            // Bid's qty is smaller than offer's. Its trade is uncomplete.
            // Updates the qty for the offer order.
            offer_list_it_->qty -= bid_list_it_->qty;
            trade_.qty = bid_list_it_->qty;
            history.splice(history.begin(), bid_book_it_->second, bid_list_it_);
            if (!peg_bid_ && bid_book_it_->second.empty())
                // If the list empties
                limit_map(Side::Buy).erase(bid_book_it_->first);
        }
        else if (bid_list_it_->qty > offer_list_it_->qty) {
            // Offer's qty is smaller than bid's. Its trade is uncomplete.
            // Updates the qty for the bid order.
            bid_list_it_->qty -= offer_list_it_->qty;
            trade_.qty = offer_list_it_->qty;
            history.splice(history.begin(), offer_book_it_->second, offer_list_it_);
            if (!peg_offer_ && offer_book_it_->second.empty())
                // If the list empties
                limit_map(Side::Sell).erase(offer_book_it_->first);
        }
        else {
            trade_.qty = bid_list_it_->qty; // = offer_list_it_->qty;
            history.splice(history.begin(), bid_book_it_->second, bid_list_it_);
            history.splice(history.begin(), offer_book_it_->second, offer_list_it_);
            if (!peg_bid_ && bid_book_it_->second.empty())
                limit_map(Side::Buy).erase(bid_book_it_->first);
            if (!peg_offer_ && offer_book_it_->second.empty())
                limit_map(Side::Sell).erase(offer_book_it_->first);
        }

        // The executed price is determined by the prioritary order.
        trade_.price = (trade_.bid_order_id < trade_.offer_order_id) ? bid_book_it_->first : offer_book_it_->first;

        trades_.push_back(trade_);
    }
}
*/


