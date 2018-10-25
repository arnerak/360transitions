/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Bertrand Martel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/**
	linked_list.h

	Build custom linked list, push new element, clear and print items

	@author Bertrand Martel
	@version 0.1
*/
#ifndef LLIST
#define LLIST

#include <stdlib.h>
#include <stdio.h>
#include "SDL2/SDL.h"
#include <string>

//--------------CAPTION STRUCT --------------
typedef struct caption_item caption_item;

enum class CaptionType { value, background };

struct caption_item
{
	std::string caption_txt;
	int    caption_id;
	int    caption_color;
	CaptionType type;
	struct caption_item *nxt;
};

typedef caption_item* captionlist;
//----------------------------------------

//-------------COORDINATE STRUCT ------------
typedef struct coordinate_item coordinate_item;

struct coordinate_item
{
	float  x;
	float  y;
	int bgcolor;
	int    caption_id;
	struct coordinate_item *nxt;
};

typedef coordinate_item* coordlist;
//----------------------------------------

//-------------SDL_Surface STRUCT ------------
typedef struct surface_item surface_item;

struct surface_item
{
	SDL_Surface * surface;
	struct surface_item *nxt;
};

typedef surface_item* surfacelist;

class LinkedList
{
public:
	LinkedList()
	{

	}

	~LinkedList()
	{

	}

	/**
	* @brief clear_caption
	*      clear caption table
	* @param list
	*      list of caption items
	*/
	void clear_caption() {

		if (caption_list != NULL)
		{
			caption_item * current = caption_list;
			caption_item * next;

			while (current != NULL)
			{
				next = current->nxt;
				delete current;
				current = next;
			}

			caption_list = NULL;
		}
	}

	/**
	* @brief push_back_caption
	*      push a new item to the end of caption table
	* @param list
	*      list of caption items
	* @param caption_txt
	*      caption text
	* @param color
	*      caption color
	*/
	void push_back_caption(const std::string& txt, int caption_id, int color, CaptionType type = CaptionType::value)
	{
		caption_item* caption_new_item = new caption_item;
		caption_new_item->caption_txt = txt;
		caption_new_item->caption_id = caption_id;
		caption_new_item->caption_color = color;
		caption_new_item->type = type;
		caption_new_item->nxt = NULL;

		if (caption_list == NULL)
		{
			caption_list= caption_new_item;
		}
		else
		{
			caption_item* temp = caption_list;
			while (temp->nxt != NULL)
			{
				temp = temp->nxt;
			}
			temp->nxt = caption_new_item;
		}
	}

	/**
	* @brief print_list_caption
	*      print caption table
	* @param list
	*      list of caption items
	*/
	void print_list_caption()
	{

		caption_item *tmp = caption_list;

		if (tmp != NULL)
		{
			while (tmp != NULL)
			{
				printf("%s => %d;", tmp->caption_txt, tmp->caption_id);
				tmp = tmp->nxt;
			}
			printf("\n");
		}

	}

	/**
	* @brief clear_coord
	*      clear coordinate table
	* @param list
	*      list of coordinate items
	*/
	void clear_coord() {

		if (coordinate_list != NULL)
		{
			coordinate_item * current = coordinate_list;
			coordinate_item * next;

			while (current != NULL)
			{
				next = current->nxt;
				delete current;
				current = next;
			}

			coordinate_list = NULL;
		}
	}

	/**
	* @brief push_back_coord
	*      push a new item to the end of coordinate table
	* @param list
	*      list of coordinate items
	* @param caption_id
	*      caption identifier
	* @param x
	*      x coordinate
	* @param y
	*      y coordinate
	*/
	void push_back_coord(int caption_id, float x, float y, int bgcolor = 0xFFFFFF)
	{
		coordinate_item* coord_new_item = new coordinate_item;
		coord_new_item->x = x;
		coord_new_item->y = y;
		coord_new_item->bgcolor = bgcolor;
		coord_new_item->caption_id = caption_id;
		coord_new_item->nxt = NULL;

		if (coordinate_list == NULL)
		{
			coordinate_list = coord_new_item;
		}
		else
		{
			coordinate_item* temp = coordinate_list;
			while (temp->nxt != NULL)
			{
				temp = temp->nxt;
			}
			temp->nxt = coord_new_item;
		}
	}

	/**
	* @brief print_list_coord
	*      print coordinate table
	* @param list
	*      list of coordinate items
	*/
	void print_list_coord()
	{

		coordinate_item *tmp = coordinate_list;

		if (tmp != NULL)
		{
			while (tmp != NULL)
			{
				printf("(%f,%f) ", tmp->x, tmp->y);
				tmp = tmp->nxt;
			}
			printf("\n");
		}

	}

	/**
	* @brief clear_surface
	*      clear surface table
	* @param list
	*      list of surface items
	*/
	surfacelist clear_surface(surfacelist list) {

		if (list != NULL)
		{
			surface_item * current = list;
			surface_item * next;

			while (current != NULL)
			{
				next = current->nxt;
				SDL_FreeSurface(current->surface);
				delete current;
				current = next;
			}

			list = NULL;
		}
		return list;
	}

	/**
	* @brief push_back_surface
	*      push a new item to the end of surface table
	* @param list
	*      list of surface items
	* @param surface
	*      SDL surface ptr
	*/
	surfacelist push_back_surface(surfacelist list, SDL_Surface* surface)
	{
		surface_item* surface_new_item = new surface_item;
		surface_new_item->surface = surface;

		surface_new_item->nxt = NULL;

		if (list == NULL)
		{
			return surface_new_item;
		}
		else
		{
			surface_item* temp = list;
			while (temp->nxt != NULL)
			{
				temp = temp->nxt;
			}
			temp->nxt = surface_new_item;
			return list;
		}
	}


	captionlist caption_list = NULL;
	coordlist coordinate_list = NULL;
};

#endif