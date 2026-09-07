# Order Matching Engine

A single-asset, in-memory order matching engine written in C++20, implementing a
strict **FIFO (price/time) priority** algorithm with support for **limit**,
**market** and **pegged** orders, plus cancellation, amendment and book
visualisation.

The project is split into a **library** that contains the entire matching logic
and knows nothing about input/output, and a **command-line interface** that owns
all text formatting and the tick↔currency conversion.

```
OrderedMap.hpp    generic container: ordered map with O(1) key lookup
OrderBook.hpp     engine interface: types, order lifecycle, invariants
OrderBook.cpp     engine implementation: matching, cancel, amend, view
main.cpp          command-line interface (parsing, formatting, I/O)
OrderArena.hpp    optional allocation backend (see "Future work")
Notas/            design notebook kept during development (in Portuguese)
Rascunhos/        successive drafts, kept for the record
```

---

## 1. Building and running

```bash
g++ -std=c++20 -O2 -Wall -Wextra main.cpp OrderBook.cpp -o omr

./omr                  # interactive
./omr < script.txt     # batch
```

The engine requires only the standard library. It has been compiled clean with
`-Wall -Wextra -Wpedantic` and validated under AddressSanitizer,
UndefinedBehaviorSanitizer and libstdc++ debug mode (`-D_GLIBCXX_DEBUG`).

### Commands

| Command | Example | Response |
|---|---|---|
| `limit buy\|sell <qty> <price>` | `limit buy 100 10` | `Order created: id_0 limit buy 100 @ 10` |
| `market buy\|sell <qty>` | `market buy 150` | `Order created: id_3 market buy 150` |
| `peg [bid\|offer] buy\|sell <qty>` | `peg bid buy 150` | `Order created: id_4 peg buy 150 @ bid` |
| `cancel order <id>` | `cancel order id_0` | `Order id_0 cancelled.` |
| `change order <id> <qty> [price]` | `change order id_0 200 9.98` | `Order id_0 changed: qty 200 @ 9.98` |
| `consult order <id>` | `consult order id_0` | `Order id_0: limit buy 200 @ 9.98 active` |
| `print book` | | two-column view, best price first |
| `help`, `quit` | | |

Identifiers are printed as `id_<n>` and accepted with or without the prefix.
Blank lines and lines starting with `#` are ignored, which makes scripted
regression runs straightforward.

Whenever an operation produces executions, the engine reports them twice: once
aggregated by price (the format required by the specification) and once per
matched pair, so the two counterparties of every fill are visible.

### Worked example

The example from the specification, replayed verbatim:

```
>>> limit buy 100 10
>>> limit sell 100 20
>>> limit sell 200 20
>>> market buy 150
Trade - price: 20, qty: 150
	buy order id_3, sell order id_1 - price: 20, qty: 100
	buy order id_3, sell order id_2 - price: 20, qty: 50
>>> market buy 200
Trade - price: 20, qty: 150
	buy order id_4, sell order id_2 - price: 20, qty: 150
>>> market sell 200
Trade - price: 10, qty: 100
	buy order id_0, sell order id_5 - price: 10, qty: 100
```

And the pegged-order example, which exercises the reference-price mechanism:

```
>>> limit buy 200 10
>>> limit buy 100 9.99
>>> limit sell 100 10.5
>>> peg bid buy 150
>>> print book
Buy                          | Sell
-----------------------------|-----------------------------
id_0 limit 200 @ 10 active   | id_2 limit 100 @ 10.5 active
id_3 peg 150 @ 10 active     |
id_1 limit 100 @ 9.99 active |

>>> limit buy 300 10.1
>>> print book
Buy                          | Sell
-----------------------------|-----------------------------
id_3 peg 150 @ 10.1 active   | id_2 limit 100 @ 10.5 active
id_4 limit 300 @ 10.1 active |
id_0 limit 200 @ 10 active   |
id_1 limit 100 @ 9.99 active |
```

The peg follows the new best bid **and keeps its place in the queue ahead of the
limit order that created that price**, because it arrived earlier. No repricing
work is done: the peg's price is derived, never stored.

---

## 2. Requirements coverage

| Requirement | Where |
|---|---|
| Single asset | whole design |
| Limit and market orders | `insert_order`, `Type::Limit` / `Type::Market` |
| In-memory only, no persistence | all state in `OrderBook` members |
| Insert with type, side, price, qty | `insert_order(Type, Side, int, std::optional<int>)` |
| Crossing limit orders are filled | `run_matching`, §5 |
| Trade output | interface layer, `Cli::flush_trades` |
| Book visualisation | `book_view` + `Cli::cmd_print_book` |
| Arrival-order (time) priority | `seq` counter, §4 |
| Cancellation | `cancel_order` |
| Amendment, with requeueing on price change | `change_order`, §6 |
| Pegged orders | `peg_book_`, §7 |

---

## 3. Data model

### 3.1 `Order` — 20 bytes

```cpp
struct Order {
    // public getters …
private:
    int qty, price, id, seq;
    Type type;      // Limit | Market | Peg
    Side side;      // Sell | Buy
    Status status;  // Active | Partial | Total | Cancelled
    friend class OrderBook;
};
```

Two deliberate choices:

**Field grouping.** The compiler is not allowed to reorder members, so it pads to
satisfy alignment. Declaring `Type`/`Side` first and `Status` last would produce
two padding holes and a 24-byte struct; grouping the four `int`s first and the
three one-byte enums last yields **20 bytes** — a 17% reduction with no cost.

**Private construction.** Only `OrderBook` can build an `Order`: `id`, `seq` and
`status` belong to the book, and the (type, price) pair is only meaningful after
validation. Users receive orders through `locate_order`, which returns
`const Order*`.

Prices are integers (ticks). Conversion to currency happens exclusively at the
interface boundary, so the engine never touches floating point and never suffers
from representation error when comparing prices — a real concern in a matching
engine, where price equality decides whether a trade happens at all.

### 3.2 Identity versus priority: `id` and `seq`

Every order carries two distinct numbers, and separating them is what makes
amendments correct:

- **`id`** is the client-facing identifier. It is assigned once and never
  changes, so a trader can cancel an order after amending it.
- **`seq`** is the logical timestamp used for time priority. It is refreshed
  whenever an order *loses* priority.

Using a single number for both — the mistake in the early drafts — breaks in a
subtle way: after an amendment the order is physically moved to the back of its
queue, but its stored number still says it is old. Two pieces of the matching
logic read that number (the peg-versus-limit merge and the passive-price rule),
and both would then disagree with the actual queue order.

Time is measured in message count rather than wall-clock time: it is smaller,
faster, monotonic by construction, and immune to clock adjustments. The
resulting invariant is that **`seq` is strictly increasing within any queue**,
which is exactly what the algorithm needs.

### 3.3 `Trade` and result types

```cpp
struct Trade { int price, qty; int bid_id, offer_id; Side aggressor; };   // 20 B
struct OpResult { int id; Reject reject; bool check() const; };            // 8 B
struct BookOut { int id, qty, price; Type type; Status status; };          // 16 B
```

`Trade` records one *matched pair*, not one aggregated print. This is the
faithful record — it names both counterparties, which is what a downstream
system, a test, or an audit trail needs. Aggregating consecutive fills at the
same price into a single line is a presentation concern and lives in the
interface.

Errors are returned, never printed: the engine has no `std::cout` and no
`std::cerr`. `Reject` is a closed enum with a `to_string` written as a `switch`
with **no `default:` label**, so `-Wswitch` turns the compiler into a test — add
a new rejection reason and forget its message, and the build warns.

---

## 4. Book structure

```
lim_book_[Buy]                              lim_book_[Sell]
OrderedMap<int, list<Order>>                OrderedMap<int, list<Order>>
  key -1010 → [ord, ord, ord]  ← begin()      key  1050 → [ord]        ← begin()
  key -1000 → [ord, ord]                      key  1060 → [ord, ord]
  key  -999 → [ord]                           key  1100 → [ord]

peg_book_[Buy]  → [peg, peg]                peg_book_[Sell] → []
history_        → [terminal orders]
ids_            → vector indexed by id
```

### 4.1 Price levels: `OrderedMap`

Price levels must be **ordered** (to find the best bid and offer) and
**addressable by key** (to locate the level of an order being cancelled or
amended). A red-black tree gives ordering with `O(log n)` lookup; a hash table
gives `O(1)` lookup with no ordering.

`OrderedMap` composes both: a `std::map` holds the levels in order, and an
`std::unordered_map<Key, Map::iterator>` indexes them by key. `std::map`
iterators are stable under insertion and erasure of *other* elements, so the
index stays valid for the lifetime of a level.

- `find`, `contains`, `at`: `O(1)` — hash lookup only.
- `try_emplace` on an existing key: `O(1)` — the index is checked first, so the
  value is not even constructed.
- `try_emplace` on a new key, `erase`: `O(1)` index work plus `O(log n)` tree work.
- `begin()`: `O(1)` (`std::map` caches its leftmost node).

The extra memory is one hash node per **price level**, not per order — which is
the point. The alternative considered was storing the level handle inside every
`Order` (8 bytes per order); with more orders than levels, indexing per level is
cheaper. Section 9 quantifies the lookup gain.

### 4.2 The negated-key trick

The buy side must be ordered *descending* (highest bid first), the sell side
*ascending*. Three options were considered:

1. A comparator with a `bool desc` member. Rejected: the comparator becomes
   stateful, which kills empty-base optimisation inside `std::map` and turns
   every tree comparison into a runtime branch.
2. Two differently-typed maps (`std::greater` / `std::less`). Rejected: the two
   books become different types and every helper has to be templated.
3. **Store the buy side's keys negated.**

Option 3 was chosen. Both maps are `OrderedMap<int, OrderQueue>` with the default
stateless `std::less<int>`, `begin()` is the best price on *both* sides, and the
sign flip costs one instruction, confined to two functions:

```cpp
static constexpr int key_of(Side s, int price) noexcept { return (s == Side::Buy) ? -price : price; }
static constexpr int price_of(Side s, int key)  noexcept { return (s == Side::Buy) ? -key : key; }
```

Keeping the convention in exactly two places matters: a stray sign in a third
place was, historically, one of this project's bugs.

### 4.3 Order queues: why `std::list`

Each price level holds its orders in arrival order. The operations required are:

| Operation | When | `std::list` | `std::deque`/`std::vector` |
|---|---|---|---|
| append | new order | `O(1)` | `O(1)` amortised |
| pop front | fill | `O(1)` | `O(1)` / `O(n)` |
| **remove from the middle** | cancel | `O(1)` | `O(n)` |
| **middle → back of another queue** | amend | `O(1)` splice | `O(n)` |
| stable references | index by id | yes | no |

The last two rows decide it. Cancellation and amendment reach an order in the
middle of a queue, and amendment must move it to the back of a possibly
different queue. `std::list::splice` does that in constant time by relinking
nodes, and — crucially — **does not invalidate iterators**, so the by-id index
keeps working even after an order is executed or cancelled and moved to the
history. The price paid is one extra pointer per node and one allocation per
order; §10 discusses removing both.

### 4.4 The pegged-order queue

A *peg to the bid* order tracks the best buy price; a *peg to the offer* tracks
the best sell price. The naive implementation stores a price and rewrites it
whenever the top of book moves — which means touching every peg on every book
update.

This implementation **does not store the price of a peg at all**. Pegs live in
one queue per side, in arrival order, and their price is *derived*: it is the
best limit price of their own side, read when needed. Consequences:

- Repricing is free. When the best bid changes, every peg follows automatically,
  because nothing was stored to update.
- A peg competes exactly at the best price level, so the effective queue at the
  top of book is the **merge, by `seq`, of the best limit level and the peg
  queue**. Both are already sorted by `seq`, so the merge is a single comparison
  of two heads — `O(1)`, no allocation, nothing materialised.
- **A peg can never create a crossed book.** Its price *is* the best bid, so
  `best_bid ≥ best_offer` remains a property of the limit book alone. Inserting a
  peg therefore does not even need to run the matching loop; it can only change
  *who* fills first, never *whether* a fill happens.

### 4.5 Lookup by id: a plain `std::vector`

Ids are dense integers issued consecutively from 0, so the natural index is the
id itself:

```cpp
std::vector<OrderQueue::iterator> ids_;   // ids_[id] → the order
```

This is strictly better than a hash map here: no hashing, no buckets, no
collision handling, 8 bytes per order instead of roughly 32, and perfect
locality. The invariant that makes it work — every accepted order pushes exactly
one entry, so `id == ids_.size()` at insertion time — is asserted at each of the
three insertion sites.

`ids_` never shrinks. That is deliberate, not a leak: it is what allows
`consult order id_7` to answer for an order that was filled or cancelled long
ago.

### 4.6 History

Terminal orders (fully executed, cancelled, or market orders that finished) are
**spliced** into `history_`. No copy is made, no node is destroyed, and the
iterator held in `ids_` remains valid — the same list node simply belongs to a
different list. Order status is the discriminator: an order in `history_` is
`Total` or `Cancelled` (or `Partial` for a terminated market order).

---

## 5. The matching algorithm

### 5.1 Class invariants

The public API maintains four invariants, which is why the matching routine is
private — a caller must not be able to leave the book in an intermediate state.

- **I1.** After any public operation the book is not crossed: `best_bid < best_offer`.
- **I2.** After any public operation no empty price level survives in the map.
  During a sweep a level may be transiently empty; it is removed when the sweep
  of that level ends.
- **I3.** `ids_[id]` is valid for every id ever issued, forever.
- **I4.** Within any queue, `seq` is strictly increasing.

### 5.2 Two nested loops

```cpp
while (resolve_top_queue(Buy) && resolve_top_queue(Sell)) {   // outer: pick levels
    if (buy_q.price < sell_q.price) break;                    // no cross → done

    while (resolve_top_order(Buy) && resolve_top_order(Sell)) {   // inner: drain them
        const int price = (buy_t.order->seq < sell_t.order->seq)
                        ? buy_q.price : sell_q.price;             // passive price
        execute(*buy_t.order, *sell_t.order, price);
        if (buy_t.order->qty  == 0) retire_top_order(Buy);
        if (sell_t.order->qty == 0) retire_top_order(Sell);
    }

    if (buy_q.lim_queue->empty())  lim_map(Buy).erase(key_of(Buy, buy_q.price));
    if (sell_q.lim_queue->empty()) lim_map(Sell).erase(key_of(Sell, sell_q.price));
}
```

The split is not cosmetic; it is where the efficiency comes from. The **outer**
loop touches the tree: it reads `begin()` on both sides, converts the keys to
prices, and checks the crossing condition. The **inner** loop touches nothing but
the two queues it was handed — during a sweep the two price levels are fixed, so
there is no reason to consult the map again. A deep sweep that fills a hundred
orders at one price level performs *two* tree lookups, not two hundred.

`resolve_top_order` performs the peg/limit merge described in §4.4 and hands back
a pointer to the winning order together with the queue that owns it, which is
what lets `retire_top_order` splice from the correct list in constant time.

### 5.3 Termination

Each iteration of the inner loop executes `qty = min(bid.qty, offer.qty)`, so at
least one of the two orders reaches zero and is removed from the book. The number
of resting orders therefore decreases strictly, and the inner loop terminates.
The outer loop either exits on the crossing test or removes at least one price
level per iteration, so it terminates too.

### 5.4 Trade price

A trade executes at the price of the **passive** order — the one with the lower
`seq`, meaning the one that was resting when the other arrived. The aggressor
receives price improvement when it crossed the spread.

The justification is behavioural: the trader who posts an aggressive price knows
the book and is expressing urgency, not an intent to pay more than necessary.
Awarding the resting order its posted price is also what protects time priority
from being gamed — otherwise a passive order could be systematically
disadvantaged by aggressive quoting.

Because a peg has no stored price, the rule reads the price from the *level*
rather than from the order, which handles limit and pegged orders uniformly.

---

## 6. Order type semantics

### 6.1 Limit

Rests in the book at its price. If it crosses on arrival it is filled, walking as
many price levels as its price permits; any remainder rests at its own price.

### 6.2 Market — immediate or cancel

A market order **never enters the book**. It walks the opposite side, level by
level, filling at each level's price, and **any unfilled remainder is
discarded**.

This is derived from the specification's own example rather than assumed. In it,
`market buy 200` finds only 150 shares available. If the remaining 50 had rested
as a limit order at 20, the subsequent `market sell 200` would have produced two
trade lines — one at 20 against that remainder and one at 10. The expected output
shows a single line at 10, so the remainder must have been discarded. The
"market-limit" interpretation, in which the residue converts to a limit order,
was tested and contradicts the example.

Placing market orders in the book with a sentinel price (`INT_MAX`) was also
considered and rejected: a sentinel level becomes the best price and breaks the
pegged-order merge, since pegs would then compete against a price that does not
exist. The dedicated walk avoids the problem and reuses the same primitives as
the main loop.

If the opposite side has no limit price at all, the order is rejected with
`NoMarketPrice` and recorded as cancelled — it still receives an id, so it can be
inspected afterwards.

### 6.3 Pegged

Only *peg to the bid on the buy side* and *peg to the offer on the sell side* are
accepted. The mirrored combinations (`peg bid sell`, `peg offer buy`) would price
an order at the opposite side's best price, making it permanently marketable —
behaviourally a market order that never expires. Supporting them properly would
require a separate always-aggressive queue; the interface rejects them explicitly
rather than silently accepting a wrong interpretation.

The reference-price rule, stated precisely:

> The reference price of a pegged order is the best limit price of its own side
> **at the start of a matching sweep**. If there is no limit price on that side
> when the sweep begins, the peg is inactive. A peg queued behind the limit
> orders of a level remains eligible at that level's price until the end of the
> sweep, even if those limit orders are exhausted mid-sweep.

The second sentence is the documented simplification for a peg with no valid
reference: rather than inventing a fallback (last traded price, a protective far
peg), the order simply waits. This mirrors how real systems treat an "unpriced"
peg when no reference quote is available. The third sentence describes the
snapshot behaviour: within one sweep the reference is fixed, so a peg sitting
behind the limit orders at the touch is filled at the price it was quoting when
the aggressor arrived.

---

## 7. Cancellation and amendment

**Cancellation** locates the order through `ids_` in `O(1)`, splices it into the
history, and removes the price level if it became empty. No matching is
triggered: removing liquidity can never create a cross. Note that cancelling the
best bid does not orphan a pegged buy order — the peg simply follows the new best
bid downwards, at no cost, because its price was never stored.

**Amendment** follows the CME rule: an order loses priority when its **price
changes** or its **quantity increases**; a quantity *reduction* keeps its place
in the queue. The rationale is that reducing size takes nothing from the orders
behind it, whereas increasing size or improving price would let a resting order
jump the queue for free.

Losing priority means two things happen together: `seq` is refreshed from the
book clock, and the order is spliced to the back of its (possibly new) queue.
Doing only one of them is the bug described in §3.2. The `id` is untouched, so
the client's handle survives the amendment.

Matching is re-run only when the price changed — a quantity increase at an
unchanged price cannot cross a book that was not already crossed.

One ordering detail is load-bearing: the splice happens **before** the
empty-level test. Testing first would always find the old level non-empty (the
order being moved is still in it), leaving a dead node in the map — and an empty
level at `begin()` breaks the top-of-book resolution.

---

## 8. Complexity summary

*n* = number of distinct price levels on a side; *m* = number of orders at a level.

| Operation | Time | Notes |
|---|---|---|
| Insert limit, level exists | `O(1)` | hash hit + list append |
| Insert limit, new level | `O(log n)` | tree insertion |
| Insert peg | `O(1)` | append; no matching needed |
| Best bid / best offer | `O(1)` | `std::map::begin()` |
| Match one pair | `O(1)` | no map access in the inner loop |
| Remove an emptied level | `O(log n)` | once per level, at the end of a sweep |
| Locate order by id | `O(1)` | direct vector index |
| Cancel | `O(1)` | `O(log n)` only if the level empties |
| Amend, same price | `O(1)` | splice within the queue |
| Amend, new price | `O(1)`/`O(log n)` | `O(log n)` only if the new level is new |
| Book view | `O(orders)` | one pass, in priority order |

Memory is `O(1)` per order (one list node plus one index slot), `O(1)` per price
level (one tree node plus one hash node), and no allocation happens at all in the
matching inner loop.

---

## 9. Measurements

All figures below were produced on Linux x86-64, GCC 13.3, `-O2`, in a shared
container — treat the absolute values as indicative and the *ratios* and *shapes*
as the result. Benchmarks generate the message flow with a fixed seed **before**
the timed section, so the random number generator is not measured.

### 9.1 Does `OrderedMap` pay for itself?

Time per lookup, against `std::map` alone, as a function of the number of price
levels:

| levels | `OrderedMap::find` | `std::map::find` | `OrderedMap::try_emplace` (hit) | `std::map::try_emplace` (hit) |
|---:|---:|---:|---:|---:|
| 8 | 3.16 ns | 15.74 ns | 3.15 ns | 14.82 ns |
| 64 | 2.99 ns | 26.99 ns | 2.98 ns | 26.48 ns |
| 512 | 3.14 ns | 47.35 ns | 3.15 ns | 43.17 ns |
| 4 096 | 3.18 ns | 90.01 ns | 3.22 ns | 84.44 ns |
| 32 768 | 8.92 ns | 195.08 ns | 11.31 ns | 212.70 ns |

The hash index is flat; the tree grows like `log n` **with a cache miss per
level**, which is why the gap widens so fast. Two honest caveats: the keys are
drawn uniformly across all levels, which maximises the tree's cache misses,
whereas real flow concentrates near the touch; and the composition costs one
extra hash lookup on the insertion miss path. The reason it still pays is that in
real order flow cancellations and amendments — the operations that use `find` —
vastly outnumber executions.

### 9.2 Engine throughput

Synthetic flow of 2 000 000 messages, 60% insert / 20% cancel / 20% amend, with
market and pegged orders mixed in:

| price levels | wide spread (few crossings) | tight (≈0.5 trades per message) |
|---:|---:|---:|
| 20 | ≈120 ns/op | ≈150–250 ns/op |
| 100 | ≈128 ns/op | ≈156–174 ns/op |
| 1 000 | ≈127 ns/op | ≈166–194 ns/op |

Roughly **7–8 million messages per second** when few messages cross, and 4–6
million in a flow where half the messages produce a trade — far more aggressive
than any real book.

The important result is not the absolute number but that **throughput is flat
from 20 to 1 000 price levels**. That is the design claim made empirically: book
depth does not enter the cost of a message.

### 9.3 Correctness testing

| Check | Result |
|---|---|
| Specification example (trades) | matches |
| Pegged-order example (book layout) | matches |
| Market order sweeping several levels | correct prices per level |
| Cancel / amend / consult, including error paths | correct |
| 60 000 randomised operations, ASan + UBSan | clean |
| Book never crossed (checked after every operation) | holds |
| No order with `qty ≤ 0` visible in the book | holds |
| `-D_GLIBCXX_DEBUG` (container preconditions) | clean |
| **Quantity conservation** | exact |

The conservation test is the strongest of these. Over 37 554 orders it sums, for
every order ever issued, the difference between the quantity submitted and the
quantity remaining, and compares it to twice the traded volume (each trade
decrements both sides):

```
orders = 37554   traded volume = 4292649   sum of fills = 8585298 = 2 × volume
```

No quantity is created or destroyed anywhere in the matching, cancellation or
amendment paths.

---

## 10. Design decisions, and what was rejected

| Decision | Alternative rejected | Reason |
|---|---|---|
| `OrderedMap` (tree + hash index) | plain `std::map` | `find` is flat instead of `log n`; cancels/amends dominate real flow |
| Negated buy keys | stateful comparator; two map types | stateless comparator, `begin()` symmetric, one type |
| `std::list` queues | `std::deque`, `std::vector` | `O(1)` middle removal and cross-queue splice; stable iterators |
| Peg price derived, not stored | store and rewrite on every top-of-book change | repricing becomes free; a peg can never create a cross |
| `std::vector` by-id index | `unordered_map` | ids are dense; no hashing, ¼ of the memory |
| `id` separate from `seq` | one number for both | amendment breaks the queue/priority agreement otherwise |
| Market = IOC | market-limit (residue rests) | the specification's example only closes this way |
| Market matched outside the book | sentinel price `INT_MAX` in the book | a sentinel level breaks pegged-order priority |
| Integer ticks | floating-point prices | exact price comparison; conversion at the boundary only |
| Errors returned as `Reject` | printing from the engine | the engine has no I/O; the interface owns all formatting |
| Copy constructor deleted | implicit copy | `ids_` and `OrderedMap` hold iterators into their own object |

The last row is worth spelling out, because it is a class of bug that is easy to
ship: both `ids_` and `OrderedMap`'s hash index store iterators pointing into
structures owned by the same object. An implicit copy duplicates the containers
but leaves the indices pointing at the *original* — a silently corrupt object.
Copying is therefore deleted; moving remains available and is safe, because
`std::list` and `std::map` transfer their nodes rather than reallocating them.

---

## 11. Known limitations and future work

**No self-trade prevention.** There is no notion of account or participant, so a
participant's own orders can match each other. Adding an account field and a
policy (cancel-newest, cancel-oldest, cancel-both) would be a local change to
`resolve_top_order`.

**Unbounded history.** `history_` and `ids_` grow monotonically, which is what
makes `consult` work for terminated orders. A production system would bound them
or spool them out.

**Single-threaded by design.** The specification asks for a sequential engine, and
a matching engine is inherently a serialisation point. The natural scaling path is
one engine instance per instrument with an input queue in front of it, not locks
inside the book.

**Allocation backend — `OrderArena.hpp`.** Each order is currently a separately
allocated `std::list` node: 36 bytes plus 8 bytes in `ids_`, scattered across the
heap. `OrderArena.hpp` contains a ready, tested replacement — a chunked arena
holding every order in contiguous blocks, with the list links moved *inside* the
node as two 32-bit indices (an intrusive list). Because no order is ever
destroyed in this design, slots are never recycled and the **slot is the id**,
which makes `ids_` disappear entirely.

Measured on the same flow as the engine benchmark (3 000 000 insertions with
transfer to history, nothing destroyed):

| live orders | `std::list` + `ids_` | chunked arena |
|---:|---:|---:|
| 10 000 | 35.1 ns/op | **12.3 ns/op** |
| 200 000 | 30.2 ns/op | **11.7 ns/op** |

with memory per order dropping from 44 to 28 bytes. A plain `std::vector` arena
is about 1.5 ns/op faster still, but reallocation would invalidate every
reference, so it demands a declared maximum capacity; the chunked variant
reserves each 4096-element block on creation and never resizes it, so references
are stable forever. `std::deque` was measured at 61 ns/op and rejected — its
internal blocks are too small for good locality.

The migration is mechanical and documented step by step at the end of
`OrderArena.hpp`. It is listed as future work rather than applied because it
trades the safety of `std::list` (iterators that cannot dangle, splices that
cannot corrupt) for manual link management, and that trade is only worth making
behind the test suite described in §9.3.

---

## 12. References

- CME Group, *Supported Matching Algorithms* — FIFO price/time, and the rules on
  when an amended order loses priority.
  <https://cmegroupclientsite.atlassian.net/wiki/spaces/EPICSANDBOX/pages/457218479/Supported+Matching+Algorithms>
- H. Nejati, *The Order Matching Engine: Price-Time Priority, Order Books, and
  Throughput Optimization*.
