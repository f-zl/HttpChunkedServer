#include "List.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static ListNodeBase *Node(List *l, size_t idx);

static size_t SizeOfNode(const List *l) {
  // TODO make this work even if there is padding
  return sizeof(ListNodeBase) + l->sizeofValue;
}
static bool IsValid(const List *cl) {
  List *l = (List *)cl;
  if (l->len == 0) {
    // List_Front, List_Back should not be called if len == 0
    assert(List_Begin(l) == List_End(l));
    return true;
  }
  ListNodeBase *n = List_Front(l);
  ListNodeBase *expectPrev = &l->beforeFirst;
  for (size_t len = l->len; len > 0; --len) {
    assert(n->prev == expectPrev);
    expectPrev = n;
    n = n->next;
  }
  assert(n == &l->afterLast);
  // assert(UnusedNodeNum(l) + l->len == l->maxNodeNum);
  return true;
}
static void *NodeData(ListNodeBase *node) {
  return (void *)((uintptr_t)node + sizeof(ListNodeBase));
}
static uintptr_t NodeMemory(List *l) {
#if NODE_MEMORY_IN_LIST == 0
  return (uintptr_t)l->nodeMemory;
#else
  return (uintptr_t)(l + 1);
#endif
}
static ListNodeBase *Node(List *l, size_t idx) {
  assert(idx < l->maxNodeNum);
  return (ListNodeBase *)(NodeMemory(l) + idx * SizeOfNode(l));
}
static ListNodeBase *MallocOneNode(List *l) { // return node has garbage value
  // take one node from freeNodeList
  ListNodeBase *node = l->freeNodeList;
  if (node != NULL)
    l->freeNodeList = node->next;
  return node;
}
static void FreeOneNode(List *l, ListNodeBase *n) {
  // push the node to the front of freeNodeList
  n->next = l->freeNodeList;
  if (l->freeNodeList) {
    l->freeNodeList->prev = n;
  }
  l->freeNodeList = n;
}
static void CreateFreeNodeList(List *l) {
  // link all nodes together to be a list of free nodes
  // the free node list is a mempool that takes O(1) to malloc or free
  ListNodeBase *prev = NULL;
  ListNodeBase *n = Node(l, 0);
  n->prev = prev;
  // omit prev->next = n;
  prev = n;
  for (size_t i = 1; i < l->maxNodeNum; ++i) {
    n = Node(l, i);
    n->prev = prev;
    prev->next = n;
    prev = n;
  }
  prev->next = NULL; // last node
}
void List_Init(List *l, size_t sizeofValue, size_t maxNodeNum,
               ListNodeBase *nodeMemory) {
  assert((sizeofValue > 0) && (maxNodeNum > 0) && (nodeMemory != NULL));
  l->sizeofValue = sizeofValue;
  l->maxNodeNum = maxNodeNum;
#if NODE_MEMORY_IN_LIST == 0
  l->nodeMemory = nodeMemory;
#endif
  List_Reset(l);
}
ListNodeBase *List_EmplaceBack(List *l) {
  assert(IsValid(l));
  ListNodeBase *node = MallocOneNode(l);
  if (node == NULL)
    return NULL;
  node->prev = l->afterLast.prev;
  node->next = &l->afterLast;
  l->afterLast.prev->next = node;
  l->afterLast.prev = node;
  l->len++;
  assert(IsValid(l));
  return node;
}
ListNodeBase *List_PushBack(List *l, const void *value) {
  ListNodeBase *const node = List_EmplaceBack(l);
  if (node != NULL) {
    memcpy(NodeData(node), value, l->sizeofValue);
  }
  return node;
}
ListNodeBase *List_Erase(List *l, ListNodeBase *n) {
  assert((l->len > 0) && (n != &l->beforeFirst) && (n != &l->afterLast) &&
         IsValid(l));
  n->prev->next = n->next;
  n->next->prev = n->prev;
  l->len--;
  ListNodeBase *next = n->next;
  assert(IsValid(l));
  FreeOneNode(l, n);
  return next;
}
ListNodeBase *List_Begin(List *l) { return l->beforeFirst.next; }
ListNodeBase *List_End(List *l) { return &l->afterLast; }
ListNodeBase *List_Front(List *l) {
  assert(l->len > 0);
  return List_Begin(l);
}
ListNodeBase *List_Back(List *l) {
  assert(l->len > 0);
  return List_End(l)->prev;
}
size_t List_Size(const List *l) { return l->len; }
void List_Reset(List *l) {
  l->len = 0;
  l->beforeFirst.next = &l->afterLast;
  l->afterLast.prev = &l->beforeFirst;
  l->freeNodeList = (ListNodeBase *)NodeMemory(l);
  CreateFreeNodeList(l);
}
