/*   new.c */

/*  Copyright (C)  Jean-Fran�ois Wauthy under GNU GPL
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "menueditor.h"

#include "new.h"

/************/
/* New menu */
/************/
void new_menu_cb(GtkWidget *widget, gpointer data)
{
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *header;
  GtkWidget *header_image;
  gchar *header_text;
  GtkWidget *label_filename;
  GtkWidget *entry_filename;
  GtkWidget *label_title;
  GtkWidget *entry_title;
  GtkWidget *hbox_filename;
  GtkWidget *button_browse;

  /* is there any opened menu ? */
  if(menueditor_app.xml_menu_file!=NULL){
    dialog = gtk_message_dialog_new(GTK_WINDOW(menueditor_app.main_window),
				    GTK_DIALOG_DESTROY_WITH_PARENT,
				    GTK_MESSAGE_QUESTION,
				    GTK_BUTTONS_YES_NO,
				    _("Are you sure to close the current menu ?"));

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_NO){
      gtk_widget_destroy(dialog);
      return;
    }
    
    gtk_widget_destroy(dialog);
  }

  /* Current menu has been modified, saving ? */
  if(menueditor_app.menu_modified==TRUE){
    dialog = gtk_message_dialog_new(GTK_WINDOW(menueditor_app.main_window),
				    GTK_DIALOG_DESTROY_WITH_PARENT,
				    GTK_MESSAGE_QUESTION,
				    GTK_BUTTONS_YES_NO,
				    _("Do you want to save before closing the file ?"));

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES)
      menu_save_cb(widget,data);

    gtk_widget_destroy(dialog);
  }

  /* get the filename and name for the new menu */
  
  dialog = gtk_dialog_new_with_buttons(_("New menu"),
				       GTK_WINDOW(menueditor_app.main_window),
				       GTK_DIALOG_DESTROY_WITH_PARENT,
				       GTK_STOCK_CANCEL,
				       GTK_RESPONSE_CANCEL,
				       GTK_STOCK_OK,
				       GTK_RESPONSE_OK,NULL);

  table = gtk_table_new(2,2,TRUE);
  
  /* Header */
  header_image = gtk_image_new_from_stock("gtk-justify-fill", GTK_ICON_SIZE_LARGE_TOOLBAR);
  header_text = g_strdup_printf("%s", _("New menu"));
  header = create_header_with_image (header_image, header_text);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(dialog)->vbox), header, FALSE, FALSE, 0);
  g_free (header_text);

  /* Name */
  label_title = gtk_label_new(_("Title :"));
  entry_title = gtk_entry_new();

  gtk_table_attach(GTK_TABLE(table), label_title, 0, 1, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);
  gtk_table_attach(GTK_TABLE(table), entry_title, 1, 2, 0, 1, GTK_FILL, GTK_SHRINK, 0, 0);

  /* Filename */
  label_filename = gtk_label_new(_("Filename :"));
  entry_filename = gtk_entry_new();
  button_browse = gtk_button_new_with_label("...");
  hbox_filename = gtk_hbox_new(FALSE,0);

  gtk_box_pack_start (GTK_BOX (hbox_filename), entry_filename, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox_filename), button_browse, FALSE, FALSE, 0);

  g_signal_connect ((gpointer) button_browse, "clicked", G_CALLBACK (browse_command_cb), entry_filename);

  gtk_table_attach(GTK_TABLE(table), label_filename, 0, 1, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);
  gtk_table_attach(GTK_TABLE(table), hbox_filename, 1, 2, 1, 2, GTK_FILL, GTK_SHRINK, 0, 0);

  gtk_table_set_row_spacings(GTK_TABLE(table), 5);
  gtk_table_set_col_spacings(GTK_TABLE(table), 5);
  gtk_container_set_border_width(GTK_CONTAINER(table),10);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(dialog)->vbox), table, FALSE, FALSE, 0);

  gtk_widget_show_all(dialog);

  if(gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK){
      const gchar empty_xml[]="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE xfdesktop-menu>\n<xfdesktop-menu>\n</xfdesktop-menu>";
      FILE *xml_empty_file;
      xmlNodePtr root_node, title_node;
      GtkTreeIter p, iter_root;

      /* Test if all field are filled */
      if(strlen(gtk_entry_get_text(GTK_ENTRY(entry_filename)))==0 ||
	 strlen(gtk_entry_get_text(GTK_ENTRY(entry_title)))==0){
	GtkWidget *dialog_warning = gtk_message_dialog_new (GTK_WINDOW(menueditor_app.main_window),
							    GTK_DIALOG_DESTROY_WITH_PARENT,
							    GTK_MESSAGE_WARNING,
							    GTK_BUTTONS_OK,
							    _("All the field are not filled !!!"));
	gtk_dialog_run (GTK_DIALOG (dialog_warning));
	gtk_widget_destroy (dialog_warning);
	gtk_widget_destroy (dialog);
	return;
      }

      gtk_tree_store_clear(GTK_TREE_STORE(menueditor_app.treestore));

      /* Create the new xml file */
      g_stpcpy(menueditor_app.menu_file_name,gtk_entry_get_text(GTK_ENTRY(entry_filename)));
      xml_empty_file = fopen(menueditor_app.menu_file_name,"w");
      fprintf(xml_empty_file,"%s\n",empty_xml);
      fclose(xml_empty_file);

      /* Add the node to the tree */
      menueditor_app.xml_menu_file = xmlParseFile(menueditor_app.menu_file_name);
      root_node = xmlDocGetRootElement(menueditor_app.xml_menu_file);

      if(strlen(gtk_entry_get_text(GTK_ENTRY(entry_title)))!=0){
	title_node = xmlNewNode(NULL,"title");
	xmlSetProp(title_node,"name",gtk_entry_get_text(GTK_ENTRY(entry_title)));
	xmlSetProp(title_node,"visible","yes");
	
	xmlAddChild(root_node, title_node);

	gtk_tree_store_append (menueditor_app.treestore, &p, NULL);
	gtk_tree_store_set (menueditor_app.treestore, &p, 0, NULL,
			    NAME_COLUMN, gtk_entry_get_text(GTK_ENTRY(entry_title)),
			    COMMAND_COLUMN, "", POINTER_COLUMN, title_node, -1);
      }
    
      /* Terminate operations */
      gtk_tree_view_expand_all (GTK_TREE_VIEW(menueditor_app.treeview));
    
      menueditor_app.menu_modified=TRUE;
      gtk_widget_set_sensitive(menueditor_app.main_toolbar.save,TRUE);
      gtk_widget_set_sensitive(menueditor_app.main_toolbar.close,TRUE);
      gtk_widget_set_sensitive(menueditor_app.main_toolbar.add,TRUE);
      
      gtk_widget_set_sensitive(menueditor_app.edit_menu.menu_item,TRUE);
    
      gtk_widget_set_sensitive(menueditor_app.file_menu.save,TRUE);
      gtk_widget_set_sensitive(menueditor_app.file_menu.saveas,TRUE);
      gtk_widget_set_sensitive(menueditor_app.file_menu.close,TRUE);
      gtk_widget_set_sensitive(menueditor_app.treeview,TRUE);
  }

  gtk_widget_destroy (dialog);
}
