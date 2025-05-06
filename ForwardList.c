#include "ForwardList.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static FlNodeBase *Node(FList *l, size_t idx);

static size_t SizeOfNode(const FList *l) {
  // TODO make this work even if there is padding
  return sizeof(FlNodeBase) + l->sizeofValue;
}
static bool IsValid(const FList *cl) {
  FList *l = (FList *)cl;
  if (l->len == 0) {
    assert(FL_Begin(l) == FL_End(l));
    return true;
  }
  FlNodeBase *n = FL_Front(l);
  for (size_t len = l->len; len > 0; --len) {
    n = n->next;
  }
  if (n != &l->afterLast) {
    return false;
  }
  return true;
}
static void *NodeData(FlNodeBase *node) {
  return (void *)((uintptr_t)node + sizeof(FlNodeBase));
}
static uintptr_t NodeMemory(FList *l) {
#if NODE_MEMORY_IN_LIST == 0
  return (uintptr_t)l->nodeMemory;
#else
  return (uintptr_t)(l + 1);
#endif
}
static FlNodeBase *Node(FList *l, size_t idx) {
  assert(idx < l->maxNodeNum);
  return (FlNodeBase *)(NodeMemory(l) + idx * SizeOfNode(l));
}
static FlNodeBase *MallocOneNode(FList *l) { // return node has garbage value
  // take one node from freeNodeList
  FlNodeBase *node = l->freeNodeList;
  if (node != NULL)
    l->freeNodeList = node->next;
  return node;
}
static void FreeOneNode(FList *l, FlNodeBase *n) {
  // push the node to the front of freeNodeList
  n->next = l->freeNodeList;
  l->freeNodeList = n;
}
static void CreateFreeNodeList(FList *l) {
  // link all nodes together to be a list of free nodes
  // the free node list is a mempool that takes O(1) to malloc or free

  // Node(0) must not be NULL since maxNodeNum > 0
  FlNodeBase *prev = Node(l, 0);
  for (size_t i = 1; i < l->maxNodeNum; ++i) {
    FlNodeBase *n = Node(l, i);
    prev->next = n;
    prev = n;
  }
  prev->next = NULL; // last node
}
void FL_Init(FList *l, size_t sizeofValue, size_t maxNodeNum) {
  assert((sizeofValue > 0) && (maxNodeNum > 0));
  l->sizeofValue = sizeofValue;
  l->maxNodeNum = maxNodeNum;
#if NODE_MEMORY_IN_LIST == 0
  l->nodeMemory = nodeMemory;
#endif
  FL_Clear(l);
}
FlNodeBase *FL_EmplaceFront(FList *l) {
  FlNodeBase *n = MallocOneNode(l);
  if (n != NULL) {
    n->next = l->beforeFirst.next;
    l->beforeFirst.next = n;
    ++l->len;
  }
  return n;
}
FlNodeBase *FL_PushFront(FList *l, const void *value) {
  FlNodeBase *n = FL_EmplaceFront(l);
  if (n != NULL) {
    memcpy(NodeData(n), value, l->sizeofValue);
  }
  return n;
}
FlNodeBase *FL_EmplaceAfter(FList *l, FlNodeBase *node) {
  FlNodeBase *n = MallocOneNode(l);
  if (n != NULL) {
    n->next = node->next;
    node->next = n;
    ++l->len;
  }
  return n;
}
FlNodeBase *FL_InsertAfter(FList *l, FlNodeBase *node, const void *value) {
  FlNodeBase *n = FL_EmplaceAfter(l, node);
  if (n != NULL) {
    memcpy(NodeData(n), value, l->sizeofValue);
  }
  return n;
}
FlNodeBase *FL_EraseAfter(FList *l, FlNodeBase *n) {
  assert((l->len > 0) && (n != &l->afterLast) && (n != NULL) && IsValid(l));
  --l->len;
  FlNodeBase *next = n->next;
  n->next = next->next;
  // next should not be NULL, since there is the sentinel afterList
  FreeOneNode(l, next);
  assert(IsValid(l));
  return next;
}
FlNodeBase *FL_BeforeBegin(FList *l) { return &l->beforeFirst; }
FlNodeBase *FL_Begin(FList *l) { return l->beforeFirst.next; }
FlNodeBase *FL_End(FList *l) { return &l->afterLast; }
FlNodeBase *FL_Front(FList *l) {
  assert(l->len > 0);
  return FL_Begin(l);
}
size_t FL_Size(const FList *l) { return l->len; }
void FL_Clear(FList *l) {
  l->len = 0;
  l->beforeFirst.next = &l->afterLast;
  l->freeNodeList = (FlNodeBase *)NodeMemory(l);
  CreateFreeNodeList(l);
}
