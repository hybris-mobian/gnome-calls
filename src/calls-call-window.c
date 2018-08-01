/*
 * Copyright (C) 2018 Purism SPC
 *
 * This file is part of Calls.
 *
 * Calls is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Calls is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Calls.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *     Bob Ham <bob.ham@puri.sm>
 *     Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "calls-call-window.h"
#include "calls-origin.h"
#include "calls-call-holder.h"
#include "calls-call-selector-item.h"
#include "calls-new-call-box.h"
#include "util.h"

#include <glib/gi18n.h>
#include <glib-object.h>

#define HANDY_USE_UNSTABLE_API
#include <handy.h>


struct _CallsCallWindow
{
  GtkApplicationWindow parent_instance;

  GListStore *call_holders;
  CallsCallHolder *focus;

  GtkInfoBar *info;
  GtkLabel *info_label;

  GtkStack *main_stack;
  GtkStack *header_bar_stack;
  GtkButton *show_calls;
  GtkStack *call_stack;
  GtkFlowBox *call_selector;
};

enum {
  ORIGIN_STORE_COLUMN_NAME,
  ORIGIN_STORE_COLUMN_ORIGIN
};

G_DEFINE_TYPE (CallsCallWindow, calls_call_window, GTK_TYPE_APPLICATION_WINDOW);


CallsCallWindow *
calls_call_window_new (GtkApplication *application)
{
  return g_object_new (CALLS_TYPE_CALL_WINDOW,
                       "application", application,
                       NULL);
}


static void
update_visibility (CallsCallWindow *self)
{
  guint calls = g_list_model_get_n_items (G_LIST_MODEL (self->call_holders));

  gtk_widget_set_visible (GTK_WIDGET (self), calls > 0);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_calls), calls > 1);

  if (calls == 0)
    {
      gtk_stack_set_visible_child_name (self->main_stack, "calls");
    }
  else if (calls == 1)
    {
      gtk_stack_set_visible_child_name (self->main_stack, "active-call");
    }
}


static void
show_message (CallsCallWindow *self,
              const gchar     *text,
              GtkMessageType   type)
{
  gtk_info_bar_set_message_type (self->info, type);
  gtk_label_set_text (self->info_label, text);
  gtk_widget_show (GTK_WIDGET (self->info));
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}


static void
info_response_cb (GtkInfoBar      *infobar,
                  gint             response_id,
                  CallsCallWindow *self)
{
  gtk_widget_hide (GTK_WIDGET (self->info));
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}


static GtkWidget *
call_holders_create_widget_cb (CallsCallHolder *holder,
                               CallsCallWindow *self)
{
  return GTK_WIDGET (calls_call_holder_get_selector_item (holder));
}


static void
new_call_submitted_cb (CallsCallWindow *self,
                       CallsOrigin     *origin,
                       const gchar     *number,
                       CallsNewCallBox *new_call_box)
{
  g_return_if_fail (CALLS_IS_CALL_WINDOW (self));

  calls_origin_dial (origin, number);
}


typedef gboolean (*FindCallHolderFunc) (CallsCallHolder *holder,
                                        gpointer user_data);


static gboolean
find_call_holder_by_call (CallsCallHolder *holder,
                          gpointer         user_data)
{
  CallsCallData *data = calls_call_holder_get_data (holder);

  return calls_call_data_get_call (data) == user_data;
}


/** Search through the list of call holders, returning the total
    number of items in the list, the position of the holder within the
    list and a pointer to the holder itself. */
static gboolean
find_call_holder (CallsCallWindow     *self,
                  guint               *n_itemsp,
                  guint               *positionp,
                  CallsCallHolder    **holderp,
                  FindCallHolderFunc   predicate,
                  gpointer             user_data)
{
  GListModel * const model = G_LIST_MODEL (self->call_holders);
  const guint n_items = g_list_model_get_n_items (model);
  guint position = 0;
  CallsCallHolder *holder;

  for (position = 0; position < n_items; ++position)
    {
      holder = CALLS_CALL_HOLDER (g_list_model_get_item (model, position));

      if (predicate (holder, user_data))
        {
#define out(var)                                \
          if (var##p)                           \
            {                                   \
              *var##p = var ;                   \
            }

          out (n_items);
          out (position);
          out (holder);

#undef out

          return TRUE;
        }
    }

  return FALSE;
}


static void
set_focus (CallsCallWindow *self,
           CallsCallHolder *holder)
{
  if (!holder)
    {
      holder = g_list_model_get_item (G_LIST_MODEL (self->call_holders), 0);

      if (!holder)
        {
          /* No calls */
          self->focus = NULL;
          return;
        }
    }

  self->focus = holder;

  gtk_stack_set_visible_child_name (self->main_stack, "active-call");
  gtk_stack_set_visible_child_name (self->header_bar_stack, "active-call");
  gtk_stack_set_visible_child
    (self->call_stack,
     GTK_WIDGET (calls_call_holder_get_display (holder)));
}


static void
show_calls_clicked_cb (GtkButton       *button,
                       CallsCallWindow *self)
{
  /* FIXME Setting only one of them should be enough as the properties are binded. */
  gtk_stack_set_visible_child_name (self->main_stack, "calls");
  gtk_stack_set_visible_child_name (self->header_bar_stack, "calls");
}


static void
call_selector_child_activated_cb (GtkFlowBox      *box,
                                  GtkFlowBoxChild *child,
                                  CallsCallWindow *self)
{
  GtkWidget *widget = gtk_bin_get_child (GTK_BIN (child));
  CallsCallSelectorItem *item = CALLS_CALL_SELECTOR_ITEM (widget);
  CallsCallHolder *holder = calls_call_selector_item_get_holder (item);

  update_visibility (self);
  set_focus (self, holder);
}


void
calls_call_window_add_call (CallsCallWindow *self,
                            CallsCall       *call)
{
  CallsCallHolder *holder;
  CallsCallDisplay *display;

  g_return_if_fail (CALLS_IS_CALL_WINDOW (self));
  g_return_if_fail (CALLS_IS_CALL (call));

  g_signal_connect_swapped (call, "message",
                            G_CALLBACK (show_message), self);

  holder = calls_call_holder_new (call);

  display = calls_call_holder_get_display (holder);
  gtk_stack_add_named (self->call_stack, GTK_WIDGET (display),
                       calls_call_get_number (call));

  g_list_store_append (self->call_holders, holder);

  update_visibility (self);
  set_focus (self, holder);
}


static void
remove_call_holder (CallsCallWindow *self,
                    guint            n_items,
                    guint            position,
                    CallsCallHolder *holder)
{
  g_list_store_remove (self->call_holders, position);
  gtk_container_remove (GTK_CONTAINER (self->call_stack),
                        GTK_WIDGET (calls_call_holder_get_display (holder)));

  update_visibility (self);
  if (self->focus == holder)
    {
      set_focus (self, NULL);
    }
}

void
calls_call_window_remove_call (CallsCallWindow *self,
                               CallsCall       *call,
                               const gchar     *reason)
{
  guint n_items, position;
  CallsCallHolder *holder;
  gboolean found;

  g_return_if_fail (CALLS_IS_CALL_WINDOW (self));
  g_return_if_fail (CALLS_IS_CALL (call));

  found = find_call_holder (self, &n_items, &position, &holder,
                            find_call_holder_by_call, call);
  g_return_if_fail (found);

  remove_call_holder (self, n_items, position, holder);

  if (!reason)
    {
      reason = "Call ended for unknown reason";
    }
  show_message(self, reason, GTK_MESSAGE_INFO);
}


static void
remove_calls (CallsCallWindow *self)
{
  GList *children, *child;

 /* Safely remove the call stack's children. */
  children = gtk_container_get_children (GTK_CONTAINER (self->call_stack));
  for (child = children; child != NULL; child = child->next)
    gtk_container_remove (GTK_CONTAINER (self->call_stack),
                          GTK_WIDGET (child->data));
  g_list_free (children);

  g_list_store_remove_all (self->call_holders);

  update_visibility (self);
}


static void
constructed (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (GTK_TYPE_APPLICATION_WINDOW);
  CallsCallWindow *self = CALLS_CALL_WINDOW (object);

  gtk_flow_box_bind_model (self->call_selector,
                           G_LIST_MODEL (self->call_holders),
                           (GtkFlowBoxCreateWidgetFunc) call_holders_create_widget_cb,
                           NULL, NULL);

  parent_class->constructed (object);
}


static void
calls_call_window_init (CallsCallWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->call_holders = g_list_store_new (CALLS_TYPE_CALL_HOLDER);

  update_visibility (self);
}


static void
dispose (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (GTK_TYPE_APPLICATION_WINDOW);
  CallsCallWindow *self = CALLS_CALL_WINDOW (object);

  if (self->call_holders)
    {
      remove_calls (self);
    }

  g_clear_object (&self->call_holders);

  parent_class->dispose (object);
}


static void
calls_call_window_class_init (CallsCallWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = constructed;
  object_class->dispose = dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/sm/puri/calls/ui/call-window.ui");
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, info);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, info_label);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, main_stack);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, header_bar_stack);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, show_calls);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, call_stack);
  gtk_widget_class_bind_template_child (widget_class, CallsCallWindow, call_selector);
  gtk_widget_class_bind_template_callback (widget_class, info_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, call_selector_child_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_calls_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_call_submitted_cb);
}
