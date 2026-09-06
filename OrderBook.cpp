#include "OrderBook.hpp"

#include <algorithm>
#include <cassert>

namespace orderbook {

OrderBook::OrderBook(std::size_t capacity_hint) {
    trades_.reserve(64);
    ids_.reserve(capacity_hint);
}

/* time: O(1); memory: O(1). */
bool OrderBook::resolve_top_queue(Side s) noexcept {
    auto& top_map = lim_map(s);
    if (top_map.empty())
        return false;
    
    auto map_it = top_map.begin(); // time: O(1); memory: O(1)
    TopQueue& t_q = top_queue(s);
    t_q.lim_queue = &map_it->second;
    t_q.price = price_of(s, map_it->first);
    return true;
}

/* time: O(1); memory: O(1). */
bool OrderBook::resolve_top_order(Side s) noexcept {
    TopQueue& t_q = top_queue(s);
    OrderQueue& peg_queue_ = peg_queue(s);
    assert(t_q.lim_queue != nullptr);

    if (peg_queue_.empty() && t_q.lim_queue->empty())
        return false;

    TopOrder& t = top_order(s);
    t.is_peg = t_q.lim_queue->empty() || (!peg_queue_.empty() && peg_queue_.front().seq < t_q.lim_queue->front().seq);
    t.queue = t.is_peg ? &peg_queue_ : t_q.lim_queue;
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
    trades_.emplace_back(price, qty, bid.id, offer.id,
                            (bid.seq > offer.seq) ? Side::Buy : Side::Sell);
}

void OrderBook::run_matching() {
    // time: O(1); memory: O(1) per break-condition evaluation.
    while (resolve_top_queue(Side::Buy) && resolve_top_queue(Side::Sell)) {
        TopQueue& buy_t_q = top_queue(Side::Buy);
        TopQueue& sell_t_q = top_queue(Side::Sell);
        
        if (buy_t_q.price < sell_t_q.price)
            // There is no trade when bid < offer
            break;

        // time: O(1); memory: O(1) per break-condition evaluation.
        while (resolve_top_order(Side::Buy) && resolve_top_order(Side::Sell)) {
            // time: O(1); memory: O(1).
            TopOrder& buy_t = top_order(Side::Buy);
            TopOrder& sell_t = top_order(Side::Sell);

            const int price = (buy_t.order->seq < sell_t.order->seq)
                ? buy_t_q.price : sell_t_q.price;
            // time: O(1) amortized; memory: O(1) amortized.
            execute(*buy_t.order, *sell_t.order, price);

            // time: O(1), O(log n) when the level empties; memory: O(1).
            if (buy_t.order->qty == 0)
                retire_top_order(Side::Buy);
            if (sell_t.order->qty == 0)
                retire_top_order(Side::Sell);
        }

        // time: O(1), O(log n) when the level indeed empties; memory: O(1).
        if (buy_t_q.lim_queue->empty())
            lim_map(Side::Buy).erase(key_of(Side::Buy, buy_t_q.price)); // -buy_t_q.price
        if (sell_t_q.lim_queue->empty())
            lim_map(Side::Sell).erase(key_of(Side::Sell, sell_t_q.price)); // sell_t_q.price
    }
}

/* time: O(1); memory: O(1) */
void OrderBook::retire_top_order(Side s) {
    TopOrder& t = top_order(s);
    assert(t.order != nullptr && t.order->qty == 0);
    assert(t.order == &t.queue->front());

    // time: O(1), memory: O(1).
    history_.splice(history_.begin(), *t.queue, t.queue->begin());

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
            peg_queue(side).emplace_back(order);
            // Create the index.
            assert(order.id == static_cast<int>(ids_.size()));
            ids_.push_back(std::prev(peg_queue(side).end())); // time: O(1) amortized; memory: O(1) amortized.
            break;
        }

        case Type::Limit: {
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Inserts inline the price in the (order.side) OrderMap. Returns the iterator
            // for the created list or for the existing.
            auto map_it = lim_map(side).try_emplace(key_of(side, order.price)).first;
            // Insert the order to the back of the (side) limit book.
            map_it->second.emplace_back(order); // time: O(1); memory: O(1)
            // Create the index.
            assert(order.id == static_cast<int>(ids_.size()));
            ids_.push_back(std::prev(map_it->second.end())); // time: O(1) amortized; memory: O(1) amortized.

            run_matching();
            break;
        }

        case Type::Market: {
            const Side opp = opposite(side);
            if (!resolve_top_queue(opp)) {
                order.status = Status::Cancelled;
                history_.emplace_front(order); // time: O(1); memory: O(1).
                // Create the index;
                assert(order.id == static_cast<int>(ids_.size()));
                ids_.push_back(history_.begin()); // time: O(1) amortized; memory: O(1) amortized.

                return {Reject::NoMarketPrice, order.id};
            }
            do {
                TopQueue& t_q = top_queue(opp);
                
                // time: O(1); memory: O(1) at each break-condition evaluation.
                while (order.qty > 0 && resolve_top_order(opp)) {
                    TopOrder& t = top_order(opp);
                    if (side == Side::Buy)
                        execute(order, *t.order, t_q.price); // time: O(1) amortized; memory: O(1) amortized.
                    else
                        execute(*t.order, order, t_q.price); // time: O(1) amortized; memory: O(1) amortized.
                    if (t.order->qty == 0)
                        retire_top_order(opp); // time: O(1); memory: O(1).
                }

                // time: O(1), O(log n) when the level indeed empties; memory: O(1).
                if (t_q.lim_queue->empty())
                    lim_map(opp).erase(key_of(opp, t_q.price));
            } while (order.qty > 0 && resolve_top_queue(opp));

            order.status = (order.qty == 0)  ? Status::Total
                         : (order.qty < qty) ? Status::Partial
                                             : Status::Cancelled;
            history_.emplace_front(order); // time: O(1); memory: O(1).
            assert(order.id == static_cast<int>(ids_.size()));
            ids_.push_back(history_.begin()); // time: O(1) amortized; memory: O(1) amortized.
            break;
        }
    }

    return {Reject::None, order.id};
}

OrderBook::OpResult OrderBook::cancel_order(int order_id) {
    if (order_id < 0 || static_cast<std::size_t>(order_id) >= ids_.size())
        return {Reject::NotFound};

    OrderQueue::iterator it_queue = ids_[order_id];
    if (it_queue->status == Status::Cancelled)
        return Reject::AlreadyCancelled;
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
    it_queue->status = Status::Cancelled;

    return {Reject::None, order_id};
}

OrderBook::OpResult OrderBook::change_order(int order_id, int new_qty,
                                            std::optional<int> new_price) {
    if (new_qty <= 0)
        return Reject::InvalidQty;

    if (order_id < 0 || static_cast<std::size_t>(order_id) >= ids_.size())
        return {Reject::NotFound};

    OrderQueue::iterator it_queue = ids_[order_id];
    if (it_queue->status == Status::Cancelled)
        return Reject::AlreadyCancelled;
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

    return {Reject::None, order_id};
}

void OrderBook::book_view(Side s, std::vector<BookOut>& view) const {
    view.clear();

    const BookMap& lim_map_ = lim_map(s);
    const OrderQueue& peg_queue_ = peg_queue(s);

    if (lim_map_.empty())
        // Iterates over peg queue.
        // No need to check if peg_queue_ is empty; the loop breaks naturally.
        for (const Order& peg_order : peg_queue_)
            view.emplace_back(peg_order.id, peg_order.qty, peg_order.price, peg_order.type, peg_order.status);
    else {
        auto it_map = lim_map_.begin();
        auto peg_queue_it = peg_queue_.begin();

        const int best_price = price_of(s, it_map->first);
        // Iterate over first limit queue (best price queue).
        for (const Order& lim_order : it_map->second) {
            while (peg_queue_it != peg_queue_.end() && peg_queue_it->seq < lim_order.seq) {
                // While the current peg order has priority.
                // time: O(1) amortized; memory: O(1) amortized.
                view.emplace_back(peg_queue_it->id, peg_queue_it->qty, best_price, peg_queue_it->type, peg_queue_it->status);
                ++peg_queue_it;
            }
            // time: O(1) amortized; memory: O(1) amortized.
            view.emplace_back(lim_order.id, lim_order.qty, lim_order.price, lim_order.type, lim_order.status);
        }
        // Iterate over the remainer peg queue.
        for (; peg_queue_it != peg_queue_.end(); ++peg_queue_it)
            // time: O(1) amortized; memory: O(1) amortized.
            view.emplace_back(peg_queue_it->id, peg_queue_it->qty, best_price, peg_queue_it->type, peg_queue_it->status);
        // Iterate over the map, according to price order (price priority).
        for (++it_map; it_map != lim_map_.end(); ++it_map)
            // Iterate over the current limit queue.
            for (const Order& lim_order : it_map->second)
                // time: O(1) amortized; memory: O(1) amortized.
                view.emplace_back(lim_order.id, lim_order.qty, lim_order.price, lim_order.type, lim_order.status);
    }
}

const Order* OrderBook::locate_order(int order_id) const {
    return (order_id < 0 || static_cast<std::size_t>(order_id) >= ids_.size()) ? nullptr : &*ids_[order_id];
}

} // namespace orderbook
