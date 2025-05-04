#pragma once
#include <assert.h> // use static_assert to be C++ compatible
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
 * struct FlNodeInt {
 *   FlNodeInt base;
 *   int value;
 * };
 */
typedef struct FlNodeBase {
  struct FlNodeBase *next;
} FlNodeBase;

#define DEFINE_LIST_NODE_TYPE(NodeType, ValueType)                             \
  typedef struct NodeType {                                                    \
    FlNodeBase base;                                                           \
    ValueType value;                                                           \
  } NodeType;                                                                  \
  static_assert(                                                               \
      sizeof(FlNodeBase) + sizeof(ValueType) == sizeof(NodeType),              \
      "current implementation requires no padding in the top NodeType");

#define NODE_MEMORY_IN_LIST 1

typedef struct FList {
  size_t len;               // num of active nodes
  FlNodeBase beforeFirst;   // sentinel
  FlNodeBase afterLast;     // sentinel
  FlNodeBase *freeNodeList; // a list of free nodes, as a mempool
  /*const*/ size_t sizeofValue;
  /*const*/ size_t maxNodeNum;
#if NODE_MEMORY_IN_LIST == 0
  void * /*const*/ nodeMemory; // nodeMemory should be the address of an array
                               // of derived struct of FlNodeBase
                               // e.g. address of `ListNodeInt memory[5]`
                               // use void * here to disable nodeMemory[i]
#endif
} FList;

#if NODE_MEMORY_IN_LIST
#define DEFINE_LIST_TYPE(ListType, NodeType, maxNodeNum)                       \
  typedef struct ListType {                                                    \
    FList base;                                                                \
    NodeType nodeMemory[maxNodeNum];                                           \
  } ListType;                                                                  \
  static_assert(sizeof(ListType) ==                                            \
                    sizeof(FList) + sizeof(NodeType) * maxNodeNum,             \
                "current implementation requires no padding in ListType");

#endif

// NOTE the second argument is sizeofValue, e.g. sizeof(int)
// not node size, e.g. sizeof(FlNodeInt)
void FL_Init(FList *l, size_t sizeofValue, size_t maxNodeNum);
// the value in the node is left uninitialized
FlNodeBase *FL_EmplaceFront(FList *);
FlNodeBase *FL_PushFront(FList *, const void *value);
FlNodeBase *FL_EmplaceAfter(FList *, FlNodeBase *);
FlNodeBase *FL_InsertAfter(FList *, FlNodeBase *, const void *value);
FlNodeBase *FL_EraseAfter(FList *, FlNodeBase *);
FlNodeBase *FL_BeforeBegin(FList *);
FlNodeBase *FL_Begin(FList *);
FlNodeBase *FL_End(FList *);
// FL_Front should not be called if len == 0
FlNodeBase *FL_Front(FList *);
size_t FL_Size(const FList *);
void FL_Clear(FList *);

#ifdef __cplusplus
}
#endif
