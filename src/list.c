/* Copyright (c) 2012-2015 David Huseby
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "debug.h"
#include "macros.h"
#include "list.h"

#if defined(UNIT_TESTING)
#include "test_flags.h"
#endif

/* node used in queue structure */
struct list_item_s
{
  int_t   next;   /* next node in the list */
  int_t   prev;   /* prev node in the list */
  int_t   used;   /* bool to mark used nodes */
  void *  data;   /* pointer to the data */
};

#define ITEM_AT(items, index) (&(items[index]))

/* index constants */
list_itr_t const list_itr_end_t = -1;

/* forward declaration of private functions */
static list_itr_t remove_item(list_item_t * items, list_itr_t itr);
static list_itr_t insert_item(list_item_t * items, list_itr_t itr, list_itr_t item);
static int_t list_grow(list_t * list, uint_t amount);


/********** PUBLIC **********/

list_t * list_new(uint_t initial_capacity, list_delete_fn dfn)
{
  list_t * list = CALLOC(1, sizeof(list_t));
  CHECK_PTR_RET(list, NULL);
  if (!list_init(list, initial_capacity, dfn))
  {
    FREE(list);
    list = NULL;
  }
  return list;
}

void list_delete(void * l)
{
  CHECK_PTR(l);
  list_deinit((list_t*)l);
  FREE(l);
}

int_t list_init(list_t * list, uint_t initial_capacity, list_delete_fn dfn)
{
  CHECK_PTR_RET(list, FALSE);

  UNIT_TEST_RET(list_init);

  /* intialize the members */
  list->dfn = (dfn ? dfn : NULL);
  list->size = 0;
  list->count = 0;
  list->used_head = list_itr_end_t;
  list->free_head = list_itr_end_t;
  list->items = NULL;

  /* grow the list array if needed */
  CHECK_RET(list_grow(list, initial_capacity), FALSE);

  return TRUE;
}

int_t list_deinit(list_t * list)
{
  list_itr_t itr, end;

  UNIT_TEST_RET(list_deinit);

  CHECK_PTR_RET(list, FALSE);

  /* empty lists need no work */
  if (list->size == 0)
    return TRUE;

  if (list->dfn)
  {
    end = list_itr_end(list);
    for(itr = list_itr_begin(list); itr != end; itr = list_itr_next(list, itr))
    {
      /* call the delete function on the data at the node */
      (*(list->dfn))(ITEM_AT(list->items, itr)->data);
    }
  }

  /* reset the meta data */
  list->dfn = NULL;
  list->size = 0;
  list->count = 0;
  list->used_head = list_itr_end_t;
  list->free_head = list_itr_end_t;

  /* free the items array */
  if (list->items != NULL)
  {
    FREE(list->items);
    list->items = NULL;
  }

  return TRUE;
}

uint_t list_count(list_t const * list)
{
  UNIT_TEST_RET(list_count);

  CHECK_PTR_RET(list, 0);
  return list->count;
}

int_t list_reserve(list_t * list, uint amount)
{
  CHECK_PTR_RET(list, FALSE);

  if (amount > list->size)
  {
    return list_grow(list, (amount - list->size));
  }

  return TRUE;
}

int_t list_clear(list_t * list)
{
  list_delete_fn dfn = NULL;
  CHECK_PTR_RET(list, FALSE);

  /* remember the delete function pointer */
  dfn = list->dfn;

  /* deinit, then init the list */
  CHECK_RET(list_deinit(list), FALSE);
  CHECK_RET(list_init(list, 0, dfn), FALSE);

  return TRUE;
}

list_itr_t list_itr_begin(list_t const * list)
{
  CHECK_PTR_RET(list, list_itr_end_t);
  CHECK_RET(list->count, list_itr_end_t);
  return list->used_head;
}

list_itr_t list_itr_end(list_t const * list)
{
  return list_itr_end_t;
}

list_itr_t list_itr_tail(list_t const * list)
{
  CHECK_PTR_RET(list, list_itr_end_t);
  CHECK_RET(list->count, list_itr_end_t);

  /* the list is circular so just return the head's prev item index */
  return ITEM_AT(list->items, list->used_head)->prev;
}

list_itr_t list_itr_next(list_t const * list, list_itr_t itr)
{
  CHECK_PTR_RET(list, list_itr_end_t);
  CHECK_RET(itr != list_itr_end_t, list_itr_end_t);

  /* if the next item in the list isn't the head, return its index.
   * otherwise we're at the end of the list, return the end itr */
  return (ITEM_AT(list->items, itr)->next != list->used_head) ?
          ITEM_AT(list->items, itr)->next :
          list_itr_end_t;
}

list_itr_t list_itr_rnext(list_t const * list, list_itr_t itr)
{
  CHECK_PTR_RET(list, list_itr_end_t);
  CHECK_RET(itr != list_itr_end_t, list_itr_end_t);

  /* if the iterator isn't the head, return the previous item index.
   * otherwise we're at the reverse end of the list, return the end itr */
  return (itr != list->used_head) ?
          ITEM_AT(list->items, itr)->prev :
          list_itr_end_t;
}

int_t list_push(list_t * list, void * data, list_itr_t itr)
{
  int_t i = 0;
  list_itr_t tmp = list_itr_end_t;
  list_itr_t item = list_itr_end_t;
  list_itr_t before = itr;
  list_itr_t the_head = list_itr_end_t;

  UNIT_TEST_RET(list_push);

  CHECK_PTR_RET(list, FALSE);

  /* remember what the list head currently is */
  the_head = list->used_head;

  /* do we need to resize to accomodate this node? */
  if (list->count == list->size)
  {
    /* if there already items in the list, we need to figure out where
      * in the list the item at itr is so that we can adjust itr to be correct
      * after the list is grown since list_grow moves items around in memory. */
    if ((list->count > 0) && (itr != list_itr_end_t))
    {
      tmp = list->used_head;
      while((tmp != itr) && (i < list->count))
      {
        i++;
        tmp = list->items[tmp].next;
      }
      CHECK_RET(i != list->count, FALSE);
    }

    CHECK_RET(list_grow(list, 1), FALSE);

    /* now set the before iterator to the correct location */
    if ((list->count > 0) && (itr != list_itr_end_t))
    {
      before = list->used_head;
      while(i > 0)
      {
        before = list->items[before].next;
        i--;
      }
    }
  }

  /* get an item from the free list */
  item = list->free_head;
  list->free_head = remove_item(list->items, list->free_head);

  /* store the data pointer over */
  ITEM_AT(list->items, item)->data = data;
  ITEM_AT(list->items, item)->used = TRUE;

  /* if itr is list_itr_end_t, then we want to insert at the end of the list
    * which is just before the head... but if the list is empty, we need to
    * make sure that list_itr_end_t passes through */
  if (list->count > 0)
  {
    if (itr == list_itr_end_t)
    {
      /* insert before the head but don't update the used_head, this puts
        * the item at the tail of the list */
      before = list->used_head;
      insert_item(list->items, before, item);
    }
    else
    {
      /* if we are inserting before the head, then update used_head so that
        * the item is now the head of the list...otherwise, insert it into the
        * list and don't update the used_head. */
      if (itr == the_head)
      {
        insert_item(list->items, list->used_head, item);
        list->used_head = item;
      }
      else
        insert_item(list->items, before, item);
    }
  }
  else
  {
    /* insert into an empty list, update the used_head */
    list->used_head = insert_item(list->items, before, item);
  }

  /* update the count */
  list->count++;

  return TRUE;
}

list_itr_t list_pop(list_t * list, list_itr_t itr)
{
  /* itr can be either a valid index or list_itr_end_t.  if it is list_itr_end_t
   * they are asking to pop the tail item of the list.  the return value from
   * this function is the index of the item after the popped item or
   * list_itr_end_t if the list is empty or we popped the tail. */
  list_itr_t item = list_itr_end_t;
  list_itr_t next = list_itr_end_t;

  CHECK_PTR_RET(list, list_itr_end_t);
  CHECK_RET(list->size, list_itr_end_t);
  CHECK_RET(((itr == list_itr_end_t) || ((itr >= 0) && (itr < list->size))), list_itr_end_t);

  /* if they pass in list_itr_end_t, they want to remove the tail item */
  item = (itr == list_itr_end_t) ? list_itr_tail(list) : itr;

  /* make sure their iterator references an item in the used list */
  CHECK_RET(ITEM_AT(list->items, item)->used, list_itr_end_t);

  /* remove the item from the used list */
  next = remove_item(list->items, item);

  /* if we removed the head, update the used_head iterator */
  if (item == list->used_head)
    list->used_head = next;

  /* reset the item and put it onto the free list */
  ITEM_AT(list->items, item)->data = NULL;
  ITEM_AT(list->items, item)->used = FALSE;
  list->free_head = insert_item(list->items, list->free_head, item);

  /* update the count */
  list->count--;

  /* if we popped the tail, then we need to return list_itr_end_t, otherwise
   * we return the iterator of the next item in the list */
  return (itr == list_itr_end_t) ? list_itr_end_t : next;
}

void * list_get(list_t const * list, list_itr_t itr)
{
  UNIT_TEST_RET(list_get);

  CHECK_PTR_RET(list, NULL);
  CHECK_RET(itr != list_itr_end_t, NULL);
  CHECK_RET(list->size, NULL);                          /* empty list? */
  CHECK_RET(((itr >= 0) && (itr < list->size)), NULL);  /* valid index? */
  CHECK_RET(ITEM_AT(list->items, itr)->used, NULL);   /* in used list? */

  return ITEM_AT(list->items, itr)->data;
}


/********** PRIVATE **********/

/* removes the item at index "itr" and returns the iterator of the
 * item after the item that was removed. */
static list_itr_t remove_item(list_item_t * items, list_itr_t itr)
{
  list_itr_t next = list_itr_end_t;
  CHECK_PTR_RET(items, list_itr_end_t);
  CHECK_RET(itr != list_itr_end_t, list_itr_end_t);

  /* remove the item from the list if there is more than 1 item */
  if ((ITEM_AT(items, itr)->next != itr) &&
      (ITEM_AT(items, itr)->prev != itr))
  {
    next = ITEM_AT(items, itr)->next;
    ITEM_AT(items, ITEM_AT(items, itr)->prev)->next = ITEM_AT(items, itr)->next;
    ITEM_AT(items, ITEM_AT(items, itr)->next)->prev = ITEM_AT(items, itr)->prev;
  }

  /* clear out the item's next/prev links */
  ITEM_AT(items, itr)->next = list_itr_end_t;
  ITEM_AT(items, itr)->prev = list_itr_end_t;

  return next;
}

/* inserts the item at index "item" before the item at "itr" and returns
 * the iterator of the newly inserted item. */
static list_itr_t insert_item(list_item_t * items, list_itr_t itr, list_itr_t item)
{
  CHECK_PTR_RET(items, list_itr_end_t);
  CHECK_RET(item != list_itr_end_t, list_itr_end_t);

  /* if itr is list_itr_end_t, the list is empty, so make the
   * item the only node in the list */
  if (itr == list_itr_end_t)
  {
    ITEM_AT(items, item)->prev = item;
    ITEM_AT(items, item)->next = item;
    return item;
  }

  ITEM_AT(items, item)->next = itr;
  ITEM_AT(items, ITEM_AT(items, itr)->prev)->next = item;
  ITEM_AT(items, item)->prev = ITEM_AT(items, itr)->prev;
  ITEM_AT(items, itr)->prev = item;

  return itr;
}

static int_t list_grow(list_t * list, uint_t amount)
{
  uint_t i = 0;
  uint_t new_size = 0;
  list_item_t * items = NULL;
  list_item_t * old = NULL;
  list_itr_t itr = list_itr_end_t;
  list_itr_t end = list_itr_end_t;
  list_itr_t free_head = list_itr_end_t;
  list_itr_t used_head = list_itr_end_t;
  list_itr_t free_item = list_itr_end_t;

  CHECK_PTR_RET(list, FALSE);
  CHECK_RET(amount, TRUE); /* do nothing if grow amount is 0 */

  UNIT_TEST_RET(list_grow);

  /* figure out how big the new item array should be */
  if (list->size)
  {
    new_size = list->size;
    do
    {
      /* double the size until we have enough room */
      new_size <<= 1;

    } while(new_size < (list->size + amount));
  }
  else
    new_size = amount;

  /* try to allocate a new item array */
  items = CALLOC(new_size, sizeof(list_item_t));
  CHECK_PTR_RET(items, FALSE);

  /* initialize the new array as a free list */
  for (i = 0; i < new_size; i++)
  {
    free_head = insert_item(items, free_head, i);
  }

  /* move items from old array to new array */
  end = list_itr_end(list);
  for (itr = list_itr_begin(list); itr != end; itr = list_itr_next(list, itr))
  {
    /* get an item from the free list */
    free_item = free_head;
    free_head = remove_item(items, free_head);

    /* copy the data pointer over */
    ITEM_AT(items, free_item)->data = ITEM_AT(list->items, itr)->data;
    ITEM_AT(items, free_item)->used = ITEM_AT(list->items, itr)->used;

    /* insert the item into the data list */
    used_head = insert_item(items, used_head, free_item);
  }

  /* if we get here, we can update the list struct */
  old = list->items;
  list->items = items;
  list->size = new_size;
  list->used_head = used_head;
  list->free_head = free_head;

  /* free up the old items array */
  FREE(old);

  return TRUE;
}

#if defined(UNIT_TESTING)

#include <CUnit/Basic.h>

void test_list_private_functions(void)
{
  int_t i;
  list_item_t items[4];
  list_itr_t head = list_itr_end_t;
  MEMSET(items, 0, 4 * sizeof(list_item_t));

  /* REMOVE_ITEM TESTS */

  /* test remove_item pre-reqs */
  CU_ASSERT_EQUAL(remove_item(NULL, list_itr_end_t), list_itr_end_t);
  CU_ASSERT_EQUAL(remove_item(items, list_itr_end_t), list_itr_end_t);

  /* add some items */
  for(i = 0; i < 4; i++)
  {
    head = insert_item(items, head, i);
  }

  /* remove items from the back */
  CU_ASSERT_EQUAL(remove_item(items, 3), 0);
  CU_ASSERT_EQUAL(remove_item(items, 2), 0);
  CU_ASSERT_EQUAL(remove_item(items, 1), 0);
  CU_ASSERT_EQUAL(remove_item(items, 0), list_itr_end_t);

  /* reset head itr */
  head = list_itr_end_t;

  /* add some items */
  for(i = 0; i < 4; i++)
  {
    head = insert_item(items, head, i);
  }

  /* remove items from the front */
  CU_ASSERT_EQUAL(remove_item(items, 0), 1);
  CU_ASSERT_EQUAL(remove_item(items, 1), 2);
  CU_ASSERT_EQUAL(remove_item(items, 2), 3);
  CU_ASSERT_EQUAL(remove_item(items, 3), list_itr_end_t);

  /* INSERT_ITEM TESTS */

  /* test insert_item pre-reqs */
  CU_ASSERT_EQUAL(insert_item(NULL, list_itr_end_t, list_itr_end_t), list_itr_end_t);
  CU_ASSERT_EQUAL(insert_item(items, list_itr_end_t, list_itr_end_t), list_itr_end_t);

  /* LIST_GROW TESTS */
  CU_ASSERT_FALSE(list_grow(NULL, 0));
}

#endif

