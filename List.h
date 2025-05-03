#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
1. Use separate (fixed) memory for node
2. Maintain the memory in the list, not requiring other allocator
*/

/**
 * @brief the base struct of list node
 * Example derived struct:
 * struct ListNodeInt{
 *   ListNodeBase base;
 *   int value;
 * };
 */
typedef struct ListNodeBase {
  struct ListNodeBase *prev;
  struct ListNodeBase *next;
} ListNodeBase;

#define DEFINE_LIST_NODE_TYPE(NodeType, ValueType)                             \
  typedef struct NodeType {                                                    \
    ListNodeBase base;                                                         \
    ValueType value;                                                           \
  } NodeType;                                                                  \
  _Static_assert(                                                              \
      sizeof(ListNodeBase) + sizeof(ValueType) == sizeof(NodeType),            \
      "current implementation requires no padding in the top NodeType");

#define NODE_MEMORY_IN_LIST 1

typedef struct List {
  size_t len;                 // num of active nodes
  ListNodeBase beforeFirst;   // sentinel
  ListNodeBase afterLast;     // sentinel
  ListNodeBase *freeNodeList; // a list of free nodes, as a mempool
  /*const*/ size_t sizeofValue;
  /*const*/ size_t maxNodeNum;
#if NODE_MEMORY_IN_LIST == 0
  void * /*const*/ nodeMemory; // nodeMemory should be the address of an array
                               // of derived struct of ListNodeBase
                               // e.g. address of `ListNodeInt memory[5]`
                               // use void * here to disable nodeMemory[i]
#endif
} List;

#if NODE_MEMORY_IN_LIST
#define DEFINE_LIST_TYPE(ListType, NodeType, maxNodeNum)                       \
  typedef struct ListType {                                                    \
    List base;                                                                 \
    NodeType nodeMemory[maxNodeNum];                                           \
  } ListType;                                                                  \
  _Static_assert(sizeof(ListType) ==                                           \
                     sizeof(List) + sizeof(NodeType) * maxNodeNum,             \
                 "current implementation requires no padding in ListType");

#endif

// NOTE the second argument is sizeofValue, e.g. sizeof(int)
// not node size, e.g. sizeof(ListNodeInt)
void List_Init(List *l, size_t sizeofValue, size_t maxNodeNum,
               ListNodeBase *nodeMemory);
// the value in the node is left uninitialized
ListNodeBase *List_EmplaceBack(List *);
ListNodeBase *List_PushBack(List *, const void *value);
ListNodeBase *List_Erase(List *, ListNodeBase *);
ListNodeBase *List_Begin(List *);
ListNodeBase *List_End(List *);
// List_Front, List_Back should not be called if len == 0
ListNodeBase *List_Front(List *);
ListNodeBase *List_Back(List *);
size_t List_Size(const List *);
void List_Reset(List *);

#ifdef __cplusplus
}
#endif
