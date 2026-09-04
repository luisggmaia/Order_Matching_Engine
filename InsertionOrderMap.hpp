#ifndef INSERTION_ORDER_MAP_HPP
#define INSERTION_ORDER_MAP_HPP


template <typename Key, typename Value>
class InsertionOrderMap {
    public:
        struct Element {
            Element(Key k, Value v) : key(std::move(k)), value(std::move(v)) {}

            const Key& get_key() const { return key; }
            Value value;
            private:
                Key key;
                friend class InsertionOrderMap<Key, Value>;
        };

    using iterator = typename std::list<Element>::iterator;
    using const_iterator = typename std::list<Element>::const_iterator;

    private:
        std::list<Element> list;
        std::unordered_map<Key, iterator> index;

    public:
        bool change_key(iterator it, const Key& new_key);
        void push_back(const Key& key, const Value& value);
        void push_front(const Key& key, const Value& value);
        bool splice(const_iterator pos, InsertionOrderMap& other, iterator it);
        void pop_back();
        void pop_front();
        bool erase(const Key& key);
        void clear();

        iterator find(const Key& key);

        bool contains(const Key& key);

        size_t size();
        bool empty();

        iterator begin();
        iterator end();
        Element& front();
        Element& back();
};


#endif