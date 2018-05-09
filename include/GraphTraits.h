#ifndef ANDERSEN_GRAPHTRAITS_H
#define ANDERSEN_GRAPHTRAITS_H

// An iterator adapter that takes an iterator over a map and returns the
// corresponding iterator over the VALUE part of the map (i.e. throws away the
// KEY part). Such an adapter is useful when we want an iterator to nodes of a
// graph while internally the graph class save the nodes in the value part of a
// map
template <class MapIterator> class MapValueIterator {
private:
  MapIterator itr;
  typedef typename MapIterator::value_type::second_type MapValueType;

public:
  explicit MapValueIterator(const MapIterator &i) : itr(i) {}

  bool operator==(const MapValueIterator &other) { return itr == other.itr; }
  bool operator!=(const MapValueIterator &other) { return !(*this == other); }

  const MapValueType &operator*() { return itr->second; }
  const MapValueType &operator*() const { return itr->second; }

  const MapValueType *operator->() const { return &(itr->second); }

  // Pre-increment
  MapValueIterator &operator++() {
    ++itr;
    return *this;
  }
  // Post-increment
  const MapValueIterator operator++(int) {
    MapValueIterator ret(itr);
    ++itr;
    return ret;
  }
};

// This class should be specialized by different graph types used in Andersen's
// anlysis, which is why the default version is empty
template <class GraphType> class AndersGraphTraits {
  // Elements to provide:

  // typedef NodeType           - Type of Node in the graph
  // typedef NodeIterator       - Type used to iterator over nodes in graph
  // typedef ChildIterator      - Type used to iterate over children in graph

  // static ChildIterator child_begin(NodeType*)
  // static ChildIterator child_end(NodeType*)
  // - Return iterators that point to the beginning and ending of the child node
  // list for the specified node

  // static NodeIterator node_begin(const GraphType*)
  // static NodeIterator node_end(const GraphType*)
  // - Allow iteration over all nodes in the graph
};

#endif
