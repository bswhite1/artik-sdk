/*
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 */

#ifndef	__ARTIK_LIST_H__
#define	__ARTIK_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include "artik_error.h"

	/*! \file artik_list.h
	 *
	 * \brief Linked list implementation
	 *
	 * Implements linked-list facility to use
	 * throughout the SDK
	 *
	 */

	/*!
	 * \brief Generic linked list structure
	 *
	 * Structure containing the data elements
	 * for a generic linked list.
	 */
	typedef struct artik_list artik_list;

	/*!
	 * \brief ARTIK_LIST_HANDLE type
	 *
	 * Defines a node ID for the generic list
	 */
	typedef void *ARTIK_LIST_HANDLE;

	/*!
	 * \brief artik_list_clear_callback type
	 *
	 * Callback prototype for clear a node.
	 */

	typedef void (*artik_list_clear_callback) (artik_list *);
	/*!
	 * \brief ARTIK_LIST_FUNCB type
	 *
	 * Defines a node function pointer with two parameter and return.
	 */
	typedef void *(*ARTIK_LIST_FUNCB) (void *, void *);

	/* \brief artik_list_copy_callback
	 *
	 * Callback prototype for copy a node
	 */
	typedef void (*artik_list_copy_callback)(artik_list *dest, artik_list *src);

	struct artik_list {
		struct artik_list *next;
		artik_list_clear_callback clear;
		artik_list_copy_callback copy;
		ARTIK_LIST_HANDLE handle;
		unsigned int size_data;
		void *data;
	};

	/*!
	 * \brief ARTIK_LIST_INVALID_HANDLE
	 *
	 * Defines invalid value for a handle
	 */
	#define ARTIK_LIST_INVALID_HANDLE	NULL

	/*!
	 * \brief artik_list_add permit to add new elem to a generic list
	 *
	 * \param[in,out] list list correspond to the container to fill.
	 * \param[in,out] handle Handle value that is needed when you get or
	 * delete this node. If not defined as a specific value, it is defined
	 * as the address of the node by default.
	 * \param[in] size_of_node size_of_node specify the size in bytes of the
	 * element to add to the list.
	 *
	 * \return Node added on success, NULL otherwise
	 */
	static inline artik_list *artik_list_add(artik_list **list,
		ARTIK_LIST_HANDLE handle, int size_of_node)
	{
		artik_list *elem = *list;

		if (size_of_node < (int)sizeof(artik_list))
			return NULL;

		while ((elem != NULL) && (elem->next != NULL))
			elem = elem->next;

		if (elem) {
			elem->next = (artik_list *) malloc(size_of_node);
			if (!elem->next)
				return NULL;
			memset(elem->next, 0, size_of_node);
			elem = elem->next;
		} else {
			elem = (artik_list *) malloc(size_of_node);
			if (!elem)
				return NULL;
			memset(elem, 0, size_of_node);
			*list = elem;
		}

		if (handle != 0)
			elem->handle = handle;
		else
			elem->handle = (ARTIK_LIST_HANDLE)elem;

		elem->size_data = size_of_node;

		return elem;
	}

	/*!
	 * \brief artik_list_cpy permit to copy a list to another (plus allocate
	 * the good amount of memory)
	 *
	 * \param[in,out] list list correspond to the container to copy.
	 * \param[in,out] dest dest is the destination of the copy.
	 *
	 * \return S_OK on success, error code otherwise
	 */
	static inline artik_error artik_list_cpy(artik_list *list,
						 artik_list **dest)
	{
		artik_list *elem = list;
		artik_list *new_elem = NULL;

		if (!list || !dest || *dest)
			return E_BAD_ARGS;

		while (elem) {
			new_elem = artik_list_add(dest, elem->handle, elem->size_data);
			memcpy(new_elem, elem, elem->size_data);

			if (new_elem->copy)
				new_elem->copy(new_elem, elem);

			new_elem->next = NULL;
			elem = elem->next;
		}

		return S_OK;
	}

	/*!
	 * \brief artik_list_size permit to return the size of a generic list.
	 *
	 * \param[in,out] list list correspond to the generic container.
	 *
	 * \return The size of the list on success, error_code otherwise
	 */
	static inline unsigned int artik_list_size(artik_list *list)
	{
		artik_list *elem = list;
		unsigned int res = 0;

		while (elem) {
			++res;
			elem = elem->next;
		}
		return res;
	}

	/*!
	 * \brief artik_list_delete_node permit to delete a specific node.
	 *
	 * \param[in,out] list list correspond to the container to modify.
	 * \param[in,out] node node specify the position of the node to delete.
	 *
	 * \return S_OK on success, error code otherwise
	 */
	static inline artik_error artik_list_delete_node(artik_list **list,
							 artik_list *node)
	{
		artik_list *elem = NULL;
		artik_list *prev = NULL;

		if (!list || !*list || !node)
			return E_BAD_ARGS;

		elem = *list;

		while (elem && (elem != node)) {
			prev = elem;
			elem = elem->next;
		}

		if (!elem)
			return E_BAD_ARGS;

		if (prev != NULL)
			prev->next = elem->next;
		if (elem == *list)
			*list = elem->next;
		if (elem->clear)
			(*elem->clear) (elem);
		free(elem);

		return S_OK;
	}

	/*!
	 * \brief artik_list_delete_handle permit to delete a specific node
	 *
	 * \param[in,out] list list correspond to the container to modify.
	 * \param[in,out] handle handle specify the key of the node to delete.
	 *
	 * \return S_OK on success, error code otherwise
	 */
	static inline artik_error artik_list_delete_handle(artik_list **list,
							   ARTIK_LIST_HANDLE
							   handle)
	{
		artik_list *elem = NULL;
		artik_list *prev = NULL;

		if (!list || !*list || (handle == ARTIK_LIST_INVALID_HANDLE))
			return E_BAD_ARGS;

		elem = *list;

		while (elem && (elem->handle != handle)) {
			prev = elem;
			elem = elem->next;
		}

		if (!elem)
			return E_BAD_ARGS;
		if (prev != NULL)
			prev->next = elem->next;
		if (elem == *list)
			*list = elem->next;
		if (elem->clear)
			(*elem->clear) (elem);
		free(elem);

		return S_OK;
	}

	/*!
	 * \brief artik_list_delete_pos permit to delete a specific node
	 *
	 * \param[in,out] list list correspond to the container to modify.
	 * \param[in,out] pos pos specify the position of the node to delete.
	 *
	 * \return S_OK on success, error code otherwise
	 */
	static inline artik_error artik_list_delete_pos(artik_list **list,
							unsigned int pos)
	{
		artik_list *elem = NULL;
		artik_list *prev = NULL;

		if (!list || !*list || (pos >= artik_list_size(*list)))
			return E_BAD_ARGS;

		elem = *list;

		while (elem && pos > 0) {
			prev = elem;
			elem = elem->next;
			pos--;
		}

		if (!elem)
			return E_BAD_ARGS;
		if (prev != NULL)
			prev->next = elem->next;
		if (elem == *list)
			*list = elem->next;
		if (elem->clear)
			(*elem->clear) (elem);
		free(elem);

		return S_OK;
	}
	/*!
	 * \brief artik_list_delete_check delete a specific node of a generic
	 * list by using an external function of comparison.
	 *
	 * \param[in,out] list list correspond to the generic container.
	 * \param[in,out] check_func check_func is a custom function for
	 * comparing.
	 * \param[in,out] param_of_check param_of_check is the dynamic argument
	 * for the function 'check_func' .
	 *
	 * \return Node found on success, NULL otherwise
	 */
	static inline artik_error artik_list_delete_check(artik_list **list,
							  ARTIK_LIST_FUNCB
							  check_func,
							  void *param_of_check)
	{
		artik_list *elem = NULL;
		artik_list *prev = NULL;

		if (!list || !check_func)
			return E_BAD_ARGS;

		elem = *list;

		while (elem && ((*check_func) (elem, param_of_check) == 0)) {
			prev = elem;
			elem = elem->next;
		}

		if (!elem)
			return E_BAD_ARGS;
		if (prev != NULL)
			prev->next = elem->next;
		if (elem == *list)
			*list = elem->next;
		if (elem->clear)
			(*elem->clear) (elem);
		free(elem);

		return S_OK;
	}

	/*!
	 * \brief artik_list_delete_all permit to delete all node
	 *
	 * \param[in,out] list list correspond to the container to modify.
	 *
	 * \return S_OK on success, error code otherwise
	 */
	static inline artik_error artik_list_delete_all(artik_list **list)
	{
		artik_list *elem = NULL;

		if (!list || !*list)
			return E_BAD_ARGS;

		while (*list) {
			elem = *list;
			*list = (*list)->next;
			if (elem->clear)
				(*elem->clear) (elem);
			free(elem);
		}

		*list = NULL;

		return S_OK;
	}

	/*!
	 * \brief artik_list_get_by_handle to return a specific node of
	 * a generic list
	 *
	 * \param[in,out] list list correspond to the generic container.
	 * \param[in,out] handle handle specify the index of a node to return.
	 *
	 * \return Node found on success, NULL otherwise
	 */
	static inline artik_list *artik_list_get_by_handle(artik_list *list,
							   ARTIK_LIST_HANDLE
							   handle)
	{
		artik_list *elem = list;

		if (!elem || (handle == ARTIK_LIST_INVALID_HANDLE))
			return NULL;

		while (elem && (elem->handle != handle))
			elem = elem->next;

		return elem;
	}

	/*!
	 * \brief artik_list_get_by_pos to return a specific node of
	 * a generic list
	 *
	 * \param[in,out] list list correspond to the generic container.
	 * \param[in,out] pos pos is the position of a specific node.
	 *
	 * \return Node found on success, NULL otherwise
	 */
	static inline artik_list *artik_list_get_by_pos(artik_list *list,
							unsigned int pos)
	{
		artik_list *elem = list;
		unsigned int i = 0;

		if (!list || (pos >= artik_list_size(list)))
			return NULL;

		while (elem && i++ < pos)
			elem = elem->next;

		return elem;
	}

	/*!
	 * \brief artik_list_get_by_check return a specific node of
	 * a generic list by using an external function of comparison.
	 *
	 * \param[in,out] list list correspond to the generic container.
	 * \param[in,out] check_func check_func is a custom function for
	 * comparing the data.
	 * \param[in,out] param_of_check param_of_check is the dynamic argument
	 * for the function 'check_func'.
	 *
	 * \return Node found on success, NULL otherwise
	 */
	static inline artik_list *artik_list_get_by_check(artik_list *list,
							  ARTIK_LIST_FUNCB
							  check_func,
							  void *param_of_check)
	{
		artik_list *elem = list;

		if (!list || !check_func)
			return NULL;

		while (elem && ((*check_func) (elem, param_of_check) == 0))
			elem = elem->next;

		return elem;
	}

	/*!
	 * \brief artik_list_end permit to return the last element of
	 * a generic list
	 *
	 * \param[in,out] list list correspond to the generic container.
	 *
	 * \return Last node on success, NULL otherwise
	 */
	static inline artik_list *artik_list_end(artik_list *list)
	{
		artik_list *elem = list;

		while (elem && elem->next)
			elem = elem->next;

		return elem;
	}

#ifdef __cplusplus
}
#endif

#endif /*__ARTIK_LIST_H__ */
