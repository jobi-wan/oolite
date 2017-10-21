/*

PlistFix.c


Oolite
Copyright (C) 2004-2017 Giles C Williams and contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
MA 02110-1301, USA.

*/

#include "PlistFix.h"

#include <string.h>
#include <stdlib.h>

static const int mem_buf_eof = -1;

typedef struct
{
	char *data;
	unsigned long size,pos;
} mem_buf;

static int mem_buf_getc(mem_buf *src)
{
	if (src->pos < src->size)
		return src->data[src->pos++];
	else
		return mem_buf_eof;
}

static int mem_buf_ungetc(int c,mem_buf *src)
{
	if (!src->pos)
		return -1;
	
	src->pos--;
	src->data[src->pos] = c;
	return 0;
}

static void mem_buf_reset(mem_buf *buf)
{
	free(buf->data);
	buf->size = buf->pos = 0;
	buf->data = 0;
}

static void mem_buf_write(mem_buf *dst,char *data,unsigned long len)
{
	// TODO: realloc in larger chunks and therefor less often
	dst->data = realloc(dst->data,dst->size + len);
	memmove(dst->data + dst->size,data,len);
	dst->size += len;
}

static void mem_buf_print(mem_buf *dst,char *str)
{
	unsigned long len;
	
	len = strlen(str);
	if (!len)
		return;
	
	mem_buf_write(dst,str,len);
}

// -- token --

#define TOK_MAX_LEN	4096

typedef enum
{
	tok_none,
	tok_white,
	
	tok_cubr_open,
	tok_cubr_close,
	tok_par_open,
	tok_par_close,
	tok_equal,
	tok_semi,
	tok_comma,
	
	tok_string,
	tok_other,
	
	tok_error
} token_type;

static char token_text[TOK_MAX_LEN];

static token_type token_char(int c)
{
	switch (c)
	{
		case '{':	return tok_cubr_open;
		case '}':	return tok_cubr_close;
		case '(':	return tok_par_open;
		case ')':	return tok_par_close;
		case '=':	return tok_equal;
		case ';':	return tok_semi;
		case ',':	return tok_comma;
			
		case '"':	return tok_string;
		case mem_buf_eof:
			return tok_none;
			
		case ':':	return tok_equal;		//
		case '[':	return tok_par_open;	//
		case ']':	return tok_par_close;	//
	}
	
	if (c < 33 || c > 127)
		return tok_white;
	
	return tok_other;
}

static token_type token_read(mem_buf *src)
{
	int c,n,p;
	token_type tok = tok_white;
	
	n = 0;
	token_text[0] = 0;
	
	while (tok == tok_white)
	{
		c = mem_buf_getc(src);
		tok = token_char(c);
	}
	
	switch (tok)
	{
		case tok_none:
			return tok_none;
			
		case tok_cubr_open:
		case tok_cubr_close:
		case tok_par_open:
		case tok_par_close:
		case tok_equal:
		case tok_semi:
		case tok_comma:
			return tok;
			
		case tok_string:
			break;
			
		default:
			if (n < TOK_MAX_LEN - 1)
				token_text[n++] = c;
			break;
	}
	
	while ((1))
	{
		token_text[n] = 0;
		
		if (!strcmp(token_text,"//"))
		{
			while (c != 13 && c != 10 && c != mem_buf_eof)
				c = mem_buf_getc(src);
			
			return tok_white;
		}
		else if (!strcmp(token_text,"/*"))
		{
			p = 0;
			while (c != '/' || p != '*')
			{
				p = c;
				c = mem_buf_getc(src);
			}
			return tok_white;
		}
		
		p = c;
		c = mem_buf_getc(src);
		switch (p == '\\' ? tok_other : token_char(c))
		{
			case tok_none:
				return (tok == tok_string) ? tok_error : tok_other;
				
			case tok_other:
				break;
				
			case tok_string:
				return (tok == tok_string) ? tok_string : tok_error;
				
			default:
				if (tok != tok_string)
				{
					mem_buf_ungetc(c,src);
					return tok;
				}
				break;
		}
		if (n < TOK_MAX_LEN - 1)
			token_text[n++] = c;
	}
	
	return tok;
}

// -- node --

typedef enum
{
	node_none,
	node_dict,
	node_array,
	node_pair,
	node_val
} node_type;

struct node
{
	node_type	type;
	
	struct node *parent;
	struct node *children;
	struct node *next;
	
	char *value;
};

static void node_write(struct node *node,int depth,int newline,mem_buf *dst)
{
	int i;
	struct node *walk;
	
	switch (node->type)
	{
		case node_array:
			if (newline)
				mem_buf_print(dst,"\n");
			for (i = 0;i < depth;i++)
				mem_buf_print(dst,"  ");
			mem_buf_print(dst,"(\n");
			
			for (walk = node->children;walk;walk = walk->next)
			{
				node_write(walk,depth + 1,0,dst);
				if (walk->next)
					mem_buf_print(dst,",\n");
			}
			
			mem_buf_print(dst,"\n");
			for (i = 0;i < depth;i++)
				mem_buf_print(dst,"  ");
			mem_buf_print(dst,")");
			break;
			
		case node_dict:
			if (newline)
				mem_buf_print(dst,"\n");
			for (i = 0;i < depth;i++)
				mem_buf_print(dst,"  ");
			mem_buf_print(dst,"{\n");
			
			for (walk = node->children;walk;walk = walk->next)
			{
				node_write(walk,depth + 1,0,dst);
				mem_buf_print(dst,"\n");
			}
			
			for (i = 0;i < depth;i++)
				mem_buf_print(dst,"  ");
			mem_buf_print(dst,"}");
			break;
			
		case node_pair:
			for (i = 0;i < depth;i++)
				mem_buf_print(dst,"  ");
			
			mem_buf_print(dst,"\"");
			mem_buf_print(dst,node->value);
			mem_buf_print(dst,"\" = ");
			node_write(node->children,depth + 1,1,dst);
			mem_buf_print(dst,";");
			break;
			
		case node_val:
			if (!newline)
				for (i = 0;i < depth;i++)
					mem_buf_print(dst,"  ");
			
			mem_buf_print(dst,"\"");
			mem_buf_print(dst,node->value);
			mem_buf_print(dst,"\"");
			break;
			
		case node_none:
			break;
	}
}

static struct node *node_new(struct node *parent)
{
	struct node *node;
	
	node = malloc(sizeof *node);
	node->type = node_none;
	node->parent = parent;
	node->children = node->next = 0;
	
	if (parent)
	{
		if (parent->children)
		{
			parent = parent->children;
			while (parent->next)
				parent = parent->next;
			
			parent->next = node;
		}
		else
		{
			parent->children = node;
		}
	}
	
	node->value = 0;
	
	return node;
}

static void node_free(struct node *node)
{
	if (!node)
		return;
	
	if (node->children)
		node_free(node->children);
	if (node->next)
		node_free(node->next);
	
	if (node->value)
		free(node->value);
	
	free(node);
}

static struct node * node_up(struct node *node)
{
	struct node *parent = node->parent,*walk;
	
	if (parent != 0 && node->type == node_none)
	{
		if (parent->children == node)
		{
			parent->children = node->next;
		}
		else
		{
			for (walk = parent->children;walk;walk = walk->next)
			{
				if (walk->next == node)
				{
					walk->next = node->next;
					break;
				}
			}
		}
		node->next = 0;
		node_free(node);
	}
	return parent;
}

// -- parser --

typedef enum
{
	state_plist,
	state_obj,
	state_key,
	state_equal,
	state_comma,
	state_semi,
	state_error
} parser_state;

static void plist_write(struct node *tree,mem_buf *dst)
{
	node_write(tree,0,0,dst);
	mem_buf_print(dst,"\n");
}

static struct node *plist_read(mem_buf *src)
{
	struct node		*root,*node;
	parser_state	oldstate,newstate;
	token_type		tok;
 
	node = root = node_new(0);
	newstate = state_plist;
	
	while ((1))
	{
		oldstate = newstate;
		newstate = state_error;
		
		tok = token_read(src);
		
		switch (tok)
		{
			case tok_cubr_open:
			case tok_par_open:
				if (oldstate != state_plist && oldstate != state_obj)
					break;
				
				if (tok == tok_cubr_open)
				{
					newstate = state_key;
					node->type = node_dict;
				}
				else
				{
					newstate = state_obj;
					node->type = node_array;
				}
				node = node_new(node);
				break;
				
			case tok_cubr_close:
			case tok_par_close:
				if (tok == tok_cubr_close ? (oldstate != state_semi && oldstate != state_key) : (oldstate != state_obj && oldstate != state_comma))
					break;
				
				if (node->parent)
				{
					node = node_up(node);
					
					if (node->type != (tok == tok_cubr_close ? node_dict : node_array))
						break;
					
					if (node->parent)
					{
						node = node_up(node);
						
						if (node->type == node_pair)
						{
							node = node_new(node->parent);
							newstate = state_semi;
						}
						else if (node->type == node_array)
						{
							node = node_new(node);
							newstate = state_comma;
						}
					}
					else if (node == root)
					{
						newstate = state_plist;
					}
				}
				break;
				
			case tok_other:
			case tok_string:
				if (oldstate == state_key)
				{
					node->type = node_pair;
					node->value = strdup(token_text);
					newstate = state_equal;
				}
				else if (oldstate == state_obj)
				{
					node->type = node_val;
					node->value = strdup(token_text);
					if (node->parent)
					{
						node = node_up(node);
						
						if (node->type == node_pair)
						{
							node = node_new(node->parent);
							newstate = state_semi;
						}
						else if (node->type == node_array)
						{
							node = node_new(node);
							newstate = state_comma;
						}
					}
				}
				break;
				
			case tok_equal:
				if (oldstate != state_equal)
					break;
				
				node = node_new(node);
				newstate = state_obj;
				break;
				
			case tok_semi:
			case tok_comma:
				if (oldstate == state_comma)
					newstate = state_obj;
				else if (oldstate == state_semi)
					newstate = state_key;
				break;
				
			case tok_white:
				newstate = oldstate;
				break;
				
			case tok_none:
				if (oldstate == state_plist && node == root)
					return node;
				break;
				
			case tok_error:
				break;
		}
		
		if (newstate == state_error)
		{
			node_free(root);
			return 0;
		}
	}
}

char *plist_fix(const char *data, unsigned long *size)
{
	mem_buf buf;
	struct node *tree;
	
	buf.data = (char *)data;
	buf.size = *size;
	buf.pos = 0;
	
	tree = plist_read(&buf);
	
	if (tree)
	{
		mem_buf_reset(&buf);
		plist_write(tree,&buf);
		node_free(tree);
		*size = buf.size;
		return buf.data;
	}
	
	return 0;
}
