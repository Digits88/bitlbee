/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Jabber module - Conference rooms                                         *
*                                                                           *
*  Copyright 2007 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/

#include "jabber.h"

static xt_status jabber_chat_join_failed( struct im_connection *ic, struct xt_node *node, struct xt_node *orig );

struct groupchat *jabber_chat_join( struct im_connection *ic, const char *room, const char *nick, const char *password )
{
	struct jabber_chat *jc;
	struct xt_node *node;
	struct groupchat *c;
	char *roomjid;
	
	roomjid = g_strdup_printf( "%s/%s", room, nick );
	node = xt_new_node( "x", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_MUC );
	if( password )
		xt_add_child( node, xt_new_node( "password", password, NULL ) );
	node = jabber_make_packet( "presence", NULL, roomjid, node );
	jabber_cache_add( ic, node, jabber_chat_join_failed );
	
	if( !jabber_write_packet( ic, node ) )
	{
		g_free( roomjid );
		return NULL;
	}
	
	jc = g_new0( struct jabber_chat, 1 );
	jc->name = jabber_normalize( room );
	
	if( ( jc->me = jabber_buddy_add( ic, roomjid ) ) == NULL )
	{
		g_free( roomjid );
		g_free( jc->name );
		g_free( jc );
		return NULL;
	}
	
	/* roomjid isn't normalized yet, and we need an original version
	   of the nick to send a proper presence update. */
	jc->my_full_jid = roomjid;
	
	c = imcb_chat_new( ic, room );
	c->data = jc;
	
	return c;
}

static xt_status jabber_chat_join_failed( struct im_connection *ic, struct xt_node *node, struct xt_node *orig )
{
	struct jabber_error *err;
	struct jabber_buddy *bud;
	char *room;
	
	room = xt_find_attr( orig, "to" );
	bud = jabber_buddy_by_jid( ic, room, 0 );
	err = jabber_error_parse( xt_find_node( node->children, "error" ), XMLNS_STANZA_ERROR );
	if( err )
	{
		imcb_error( ic, "Error joining groupchat %s: %s%s%s", room, err->code,
		            err->text ? ": " : "", err->text ? err->text : "" );
		jabber_error_free( err );
	}
	if( bud )
		jabber_chat_free( jabber_chat_by_jid( ic, bud->bare_jid ) );
	
	return XT_HANDLED;
}

struct groupchat *jabber_chat_by_jid( struct im_connection *ic, const char *name )
{
	char *normalized = jabber_normalize( name );
	struct groupchat *ret;
	struct jabber_chat *jc;
	
	for( ret = ic->groupchats; ret; ret = ret->next )
	{
		jc = ret->data;
		if( strcmp( normalized, jc->name ) == 0 )
			break;
	}
	g_free( normalized );
	
	return ret;
}

void jabber_chat_free( struct groupchat *c )
{
	struct jabber_chat *jc = c->data;
	
	jabber_buddy_remove_bare( c->ic, jc->name );
	
	g_free( jc->my_full_jid );
	g_free( jc->name );
	g_free( jc );
	
	imcb_chat_free( c );
}

int jabber_chat_msg( struct groupchat *c, char *message, int flags )
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;
	
	jc->flags |= JCFLAG_MESSAGE_SENT;
	
	node = xt_new_node( "body", message, NULL );
	node = jabber_make_packet( "message", "groupchat", jc->name, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		xt_free_node( node );
		return 0;
	}
	xt_free_node( node );
	
	return 1;
}

int jabber_chat_topic( struct groupchat *c, char *topic )
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;
	
	node = xt_new_node( "subject", topic, NULL );
	node = jabber_make_packet( "message", "groupchat", jc->name, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		xt_free_node( node );
		return 0;
	}
	xt_free_node( node );
	
	return 1;
}

int jabber_chat_leave( struct groupchat *c, const char *reason )
{
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;
	struct xt_node *node;
	
	node = xt_new_node( "x", NULL, NULL );
	xt_add_attr( node, "xmlns", XMLNS_MUC );
	node = jabber_make_packet( "presence", "unavailable", jc->my_full_jid, node );
	
	if( !jabber_write_packet( ic, node ) )
	{
		xt_free_node( node );
		return 0;
	}
	xt_free_node( node );
	
	return 1;
}

void jabber_chat_invite( struct groupchat *c, char *who, char *message )
{
	struct xt_node *node;
	struct im_connection *ic = c->ic;
	struct jabber_chat *jc = c->data;

	node = xt_new_node( "reason", message, NULL ); 

	node = xt_new_node( "invite", NULL, node );
	xt_add_attr( node, "to", who ); 

	node = xt_new_node( "x", NULL, node ); 
	xt_add_attr( node, "xmlns", XMLNS_MUC_USER ); 
	
	node = jabber_make_packet( "message", NULL, jc->name, node ); 

	jabber_write_packet( ic, node ); 

	xt_free_node( node );
}

/* Not really the same syntax as the normal pkt_ functions, but this isn't
   called by the xmltree parser directly and this way I can add some extra
   parameters so we won't have to repeat too many things done by the caller
   already. */
void jabber_chat_pkt_presence( struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node )
{
	struct groupchat *chat;
	struct xt_node *c;
	char *type = xt_find_attr( node, "type" );
	struct jabber_chat *jc;
	char *s;
	
	if( ( chat = jabber_chat_by_jid( ic, bud->bare_jid ) ) == NULL )
	{
		/* How could this happen?? We could do kill( self, 11 )
		   now or just wait for the OS to do it. :-) */
		return;
	}
	
	jc = chat->data;
	
	if( type == NULL && !( bud->flags & JBFLAG_IS_CHATROOM ) )
	{
		bud->flags |= JBFLAG_IS_CHATROOM;
		/* If this one wasn't set yet, this buddy just joined the chat.
		   Slightly hackish way of finding out eh? ;-) */
		
		/* This is pretty messy... Here it sets ext_jid to the real
		   JID of the participant. Works for non-anonymized channels.
		   Might break if someone joins a chat twice, though. */
		for( c = node->children; ( c = xt_find_node( c, "x" ) ); c = c->next )
			if( ( s = xt_find_attr( c, "xmlns" ) ) &&
			    ( strcmp( s, XMLNS_MUC_USER ) == 0 ) )
			{
				struct xt_node *item;
				
				item = xt_find_node( c->children, "item" );
				if( ( s = xt_find_attr( item, "jid" ) ) )
				{
					/* Yay, found what we need. :-) */
					bud->ext_jid = jabber_normalize( s );
					break;
				}
			}
		
		/* Make up some other handle, if necessary. */
		if( bud->ext_jid == NULL )
		{
			if( bud == jc->me )
			{
				bud->ext_jid = jabber_normalize( ic->acc->user );
			}
			else
			{
				int i;
				
				/* Don't want the nick to be at the end, so let's
				   think of some slightly different notation to use
				   for anonymous groupchat participants in BitlBee. */
				bud->ext_jid = g_strdup_printf( "%s=%s", bud->resource, bud->bare_jid );
				
				/* And strip any unwanted characters. */
				for( i = 0; bud->resource[i]; i ++ )
					if( bud->ext_jid[i] == '=' || bud->ext_jid[i] == '@' )
						bud->ext_jid[i] = '_';
				
				/* Some program-specific restrictions. */
				imcb_clean_handle( ic, bud->ext_jid );
			}
			bud->flags |= JBFLAG_IS_ANONYMOUS;
		}
		
		if( bud != jc->me )
		{
			imcb_add_buddy( ic, bud->ext_jid, NULL );
			imcb_buddy_nick_hint( ic, bud->ext_jid, bud->resource );
		}
		
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0; /* Should NEVER be NULL, but who knows... */
		imcb_chat_add_buddy( chat, bud->ext_jid );
		if( s ) *s = '/';
	}
	else if( type ) /* type can only be NULL or "unavailable" in this function */
	{
		if( ( bud->flags & JBFLAG_IS_CHATROOM ) && bud->ext_jid )
		{
			s = strchr( bud->ext_jid, '/' );
			if( s ) *s = 0;
			imcb_chat_remove_buddy( chat, bud->ext_jid, NULL );
			if( bud != jc->me && bud->flags & JBFLAG_IS_ANONYMOUS )
				imcb_remove_buddy( ic, bud->ext_jid, NULL );
			if( s ) *s = '/';
		}
		
		if( bud == jc->me )
			jabber_chat_free( chat );
	}
}

void jabber_chat_pkt_message( struct im_connection *ic, struct jabber_buddy *bud, struct xt_node *node )
{
	struct xt_node *subject = xt_find_node( node->children, "subject" );
	struct xt_node *body = xt_find_node( node->children, "body" );
	struct groupchat *chat = bud ? jabber_chat_by_jid( ic, bud->bare_jid ) : NULL;
	struct jabber_chat *jc = chat ? chat->data : NULL;
	char *s;
	
	if( bud == NULL || ( jc && ~jc->flags & JCFLAG_MESSAGE_SENT && bud == jc->me ) )
	{
		char *nick;
		
		if( body == NULL || body->text_len == 0 )
			/* Meh. Empty messages aren't very interesting, no matter
			   how much some servers love to send them. */
			return;
		
		s = xt_find_attr( node, "from" ); /* pkt_message() already NULL-checked this one. */
		nick = strchr( s, '/' );
		if( nick )
		{
			/* If this message included a resource/nick we don't know,
			   we might still know the groupchat itself. */
			*nick = 0;
			chat = jabber_chat_by_jid( ic, s );
			*nick = '/';
			
			nick ++;
		}
		else
		{
			/* message.c uses the EXACT_JID option, so bud should
			   always be NULL here for bare JIDs. */
			chat = jabber_chat_by_jid( ic, s );
		}
		
		if( nick == NULL )
		{
			/* This is fine, the groupchat itself isn't in jd->buddies. */
			if( chat )
				imcb_chat_log( chat, "From conference server: %s", body->text );
			else
				imcb_log( ic, "System message from unknown groupchat %s: %s", s, body->text );
		}
		else
		{
			/* This can happen too, at least when receiving a backlog when
			   just joining a channel. */
			if( chat )
				imcb_chat_log( chat, "Message from unknown participant %s: %s", nick, body->text );
			else
				imcb_log( ic, "Groupchat message from unknown JID %s: %s", s, body->text );
		}
		
		return;
	}
	else if( chat == NULL )
	{
		/* How could this happen?? We could do kill( self, 11 )
		   now or just wait for the OS to do it. :-) */
		return;
	}
	
	if( subject )
	{
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0;
		imcb_chat_topic( chat, bud->ext_jid, subject->text_len > 0 ?
		                 subject->text : NULL, jabber_get_timestamp( node ) );
		if( s ) *s = '/';
	}
	if( body && body->text_len > 0 )
	{
		s = strchr( bud->ext_jid, '/' );
		if( s ) *s = 0;
		imcb_chat_msg( chat, bud->ext_jid, body->text, 0, jabber_get_timestamp( node ) );
		if( s ) *s = '/';
	}
}
