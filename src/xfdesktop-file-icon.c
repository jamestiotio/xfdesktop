/*
 *  xfdesktop - xfce4's desktop manager
 *
 *  Copyright(c) 2006 Brian Tarricone, <bjt23@cornell.edu>
 *  Copyright(c) 2006 Benedikt Meurer, <benny@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_LIBEXO
#define EXO_API_SUBJECT_TO_CHANGE
#include <exo/exo.h>
#endif

#include <libxfcegui4/libxfcegui4.h>

#include "xfdesktop-icon.h"
#include "xfdesktop-file-icon.h"

enum
{
    SIG_POS_CHANGED = 0,
    N_SIGS
};

struct _XfdesktopFileIconPrivate
{
    gint16 row;
    gint16 col;
    GdkPixbuf *pix;
    guint pix_opacity;
    gint cur_pix_size;
    GdkRectangle extents;
    ThunarVfsInfo *info;
    GdkScreen *gscreen;
    GList *active_jobs;
};

static void xfdesktop_file_icon_icon_init(XfdesktopIconIface *iface);
static void xfdesktop_file_icon_finalize(GObject *obj);

static GdkPixbuf *xfdesktop_file_icon_peek_pixbuf(XfdesktopIcon *icon,
                                                  gint size);
static G_CONST_RETURN gchar *xfdesktop_file_icon_peek_label(XfdesktopIcon *icon);

static void xfdesktop_file_icon_set_position(XfdesktopIcon *icon,
                                             gint16 row,
                                             gint16 col);
static gboolean xfdesktop_file_icon_get_position(XfdesktopIcon *icon,
                                                 gint16 *row,
                                                 gint16 *col);

static void xfdesktop_file_icon_set_extents(XfdesktopIcon *icon,
                                            const GdkRectangle *extents);
static gboolean xfdesktop_file_icon_get_extents(XfdesktopIcon *icon,
                                                GdkRectangle *extents);

static gboolean xfdesktop_file_icon_is_drop_dest(XfdesktopIcon *icon);
static XfdesktopIconDragResult xfdesktop_file_icon_do_drop_dest(XfdesktopIcon *icon,
                                                                XfdesktopIcon *src_icon,
                                                                GdkDragAction action);

static void xfdesktop_delete_file_finished(ThunarVfsJob *job,
                                           gpointer user_data);

static void xfdesktop_file_icon_invalidate_pixbuf(XfdesktopFileIcon *icon);

static guint __signals[N_SIGS] = { 0, };
static GdkPixbuf *xfdesktop_fallback_icon = NULL;


G_DEFINE_TYPE_EXTENDED(XfdesktopFileIcon, xfdesktop_file_icon,
                       G_TYPE_OBJECT, 0,
                       G_IMPLEMENT_INTERFACE(XFDESKTOP_TYPE_ICON,
                                             xfdesktop_file_icon_icon_init))



static void
xfdesktop_file_icon_class_init(XfdesktopFileIconClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    
    gobject_class->finalize = xfdesktop_file_icon_finalize;
    
    __signals[SIG_POS_CHANGED] = g_signal_new("position-changed",
                                              XFDESKTOP_TYPE_FILE_ICON,
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET(XfdesktopFileIconClass,
                                                              position_changed),
                                              NULL, NULL,
                                              g_cclosure_marshal_VOID__VOID,
                                              G_TYPE_NONE, 0);
}

static void
xfdesktop_file_icon_init(XfdesktopFileIcon *icon)
{
    icon->priv = g_new0(XfdesktopFileIconPrivate, 1);
    icon->priv->pix_opacity = 100;
}

static void
xfdesktop_file_icon_finalize(GObject *obj)
{
    XfdesktopFileIcon *icon = XFDESKTOP_FILE_ICON(obj);
    GtkIconTheme *itheme = gtk_icon_theme_get_for_screen(icon->priv->gscreen);
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(itheme),
                                         G_CALLBACK(xfdesktop_file_icon_invalidate_pixbuf),
                                         icon);
    
    if(icon->priv->active_jobs) {
        GList *l;
        ThunarVfsJob *job;
        for(l = icon->priv->active_jobs; l; l = l->next) {
            job = THUNAR_VFS_JOB(l->data);
            GCallback cb = g_object_get_data(G_OBJECT(job),
                                             "--xfdesktop-file-icon-callback");
            if(cb) {
                gpointer data = g_object_get_data(G_OBJECT(obj),
                                                  "-xfdesktop-file-icon-data");
                g_signal_handlers_disconnect_by_func(G_OBJECT(job),
                                                     G_CALLBACK(cb),
                                                     data);
                g_object_set_data(G_OBJECT(job),
                                  "--xfdesktop-file-icon-callback", NULL);
            }
            thunar_vfs_job_cancel(job);
            g_object_unref(G_OBJECT(job));
        }
        g_list_free(icon->priv->active_jobs);
    }
    
    if(icon->priv->pix)
        g_object_unref(G_OBJECT(icon->priv->pix));
    
    if(icon->priv->info)
        thunar_vfs_info_unref(icon->priv->info);
    
    g_free(icon->priv);
    
    G_OBJECT_CLASS(xfdesktop_file_icon_parent_class)->finalize(obj);
}

static void
xfdesktop_file_icon_icon_init(XfdesktopIconIface *iface)
{
    iface->peek_pixbuf = xfdesktop_file_icon_peek_pixbuf;
    iface->peek_label = xfdesktop_file_icon_peek_label;
    iface->set_position = xfdesktop_file_icon_set_position;
    iface->get_position = xfdesktop_file_icon_get_position;
    iface->set_extents = xfdesktop_file_icon_set_extents;
    iface->get_extents = xfdesktop_file_icon_get_extents;
    iface->is_drop_dest = xfdesktop_file_icon_is_drop_dest;
    iface->do_drop_dest = xfdesktop_file_icon_do_drop_dest;
}


static void
xfdesktop_file_icon_invalidate_pixbuf(XfdesktopFileIcon *icon)
{
    if(icon->priv->pix) {
        g_object_unref(G_OBJECT(icon->priv->pix));
        icon->priv->pix = NULL;
    }
}

XfdesktopFileIcon *
xfdesktop_file_icon_new(ThunarVfsInfo *info,
                        GdkScreen *screen)
{
    XfdesktopFileIcon *file_icon = g_object_new(XFDESKTOP_TYPE_FILE_ICON, NULL);
    file_icon->priv->info = thunar_vfs_info_ref(info);
    file_icon->priv->gscreen = screen;
    
    g_signal_connect_swapped(G_OBJECT(gtk_icon_theme_get_for_screen(screen)),
                             "changed",
                             G_CALLBACK(xfdesktop_file_icon_invalidate_pixbuf),
                             file_icon);
    
    return file_icon;
}

void
xfdesktop_file_icon_set_pixbuf_opacity(XfdesktopFileIcon *icon,
                                       guint opacity)
{
    g_return_if_fail(XFDESKTOP_IS_FILE_ICON(icon) && opacity <= 100);
    
    if(opacity == icon->priv->pix_opacity)
        return;
    
    icon->priv->pix_opacity = opacity;
    if(icon->priv->pix) {
        g_object_unref(G_OBJECT(icon->priv->pix));
        icon->priv->pix = NULL;
    }
    
    xfdesktop_icon_pixbuf_changed(XFDESKTOP_ICON(icon));
}

static GdkPixbuf *
xfdesktop_file_icon_peek_pixbuf(XfdesktopIcon *icon,
                                gint size)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    const gchar *icon_name;
    
    if(!file_icon->priv->pix || size != file_icon->priv->cur_pix_size) {
        if(file_icon->priv->pix) {
            g_object_unref(G_OBJECT(file_icon->priv->pix));
            file_icon->priv->pix = NULL;
        }

        icon_name = thunar_vfs_info_get_custom_icon(file_icon->priv->info);
        if(icon_name) {
            file_icon->priv->pix = xfce_themed_icon_load(icon_name, size);
            if(file_icon->priv->pix)
                file_icon->priv->cur_pix_size = size;
        }
            
        if(!file_icon->priv->pix) {
            icon_name = thunar_vfs_mime_info_lookup_icon_name(file_icon->priv->info->mime_info,
                                                              gtk_icon_theme_get_default());
            
            if(icon_name) {
                file_icon->priv->pix = xfce_themed_icon_load(icon_name, size);
                if(file_icon->priv->pix)
                    file_icon->priv->cur_pix_size = size;
            }
        }
        
#ifdef HAVE_LIBEXO
        if(file_icon->priv->pix_opacity != 100) {
            GdkPixbuf *tmp = exo_gdk_pixbuf_lucent(file_icon->priv->pix,
                                                   file_icon->priv->pix_opacity);
            g_object_unref(G_OBJECT(file_icon->priv->pix));
            file_icon->priv->pix = tmp;
        }
#endif
    }
    
    /* fallback */
    if(!file_icon->priv->pix) {
        if(xfdesktop_fallback_icon) {
            if(gdk_pixbuf_get_width(xfdesktop_fallback_icon) != size) {
                g_object_unref(G_OBJECT(xfdesktop_fallback_icon));
                xfdesktop_fallback_icon = NULL;
            }
        }
        if(!xfdesktop_fallback_icon) {
            xfdesktop_fallback_icon = gdk_pixbuf_new_from_file_at_size(DATADIR "/pixmaps/xfdesktop/xfdesktop-fallback-icon.png",
                                                                       size,
                                                                       size,
                                                                       NULL);
        }
        
        file_icon->priv->pix = g_object_ref(G_OBJECT(xfdesktop_fallback_icon));
        file_icon->priv->cur_pix_size = size;
        
#ifdef HAVE_LIBEXO
        if(file_icon->priv->pix_opacity != 100) {
            GdkPixbuf *tmp = exo_gdk_pixbuf_lucent(file_icon->priv->pix,
                                                   file_icon->priv->pix_opacity);
            g_object_unref(G_OBJECT(file_icon->priv->pix));
            file_icon->priv->pix = tmp;
        }
#endif
    }
    
    return file_icon->priv->pix;
}

static G_CONST_RETURN gchar *
xfdesktop_file_icon_peek_label(XfdesktopIcon *icon)
{
    return XFDESKTOP_FILE_ICON(icon)->priv->info->display_name;
}

static void
xfdesktop_file_icon_set_position(XfdesktopIcon *icon,
                                 gint16 row,
                                 gint16 col)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    
    file_icon->priv->row = row;
    file_icon->priv->col = col;
    
    g_signal_emit(G_OBJECT(file_icon), __signals[SIG_POS_CHANGED], 0, NULL);
}

static gboolean
xfdesktop_file_icon_get_position(XfdesktopIcon *icon,
                                 gint16 *row,
                                 gint16 *col)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    
    *row = file_icon->priv->row;
    *col = file_icon->priv->col;
    
    return TRUE;
}

static void
xfdesktop_file_icon_set_extents(XfdesktopIcon *icon,
                                const GdkRectangle *extents)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    
    memcpy(&file_icon->priv->extents, extents, sizeof(GdkRectangle));
}

static gboolean
xfdesktop_file_icon_get_extents(XfdesktopIcon *icon,
                                GdkRectangle *extents)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    
    if(file_icon->priv->extents.width > 0
       && file_icon->priv->extents.height > 0)
    {
        memcpy(extents, &file_icon->priv->extents, sizeof(GdkRectangle));
        return TRUE;
    }
    
    return FALSE;
}

static gboolean
xfdesktop_file_icon_is_drop_dest(XfdesktopIcon *icon)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    return (file_icon->priv->info->type == THUNAR_VFS_FILE_TYPE_DIRECTORY
            || file_icon->priv->info->flags & THUNAR_VFS_FILE_FLAGS_EXECUTABLE);
}

static void
xfdesktop_file_icon_drag_job_error(ThunarVfsJob *job,
                                   GError *error,
                                   gpointer user_data)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(user_data);
    XfdesktopFileIcon *src_file_icon = g_object_get_data(G_OBJECT(job),
                                                         "--xfdesktop-src-file-icon");
    GdkDragAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(job),
                                                             "--xfdesktop-file-icon-action"));
    gchar *primary;
    
    g_return_if_fail(file_icon && src_file_icon);
    
    if(error) {
        primary = g_strdup_printf(_("There was an error %s \"%s\" to \"%s\":"),
                                  action == GDK_ACTION_MOVE
                                  ? _("moving")
                                  : (action == GDK_ACTION_COPY
                                     ? _("copying")
                                     : _("linking")),
                                  src_file_icon->priv->info->display_name,
                                  file_icon->priv->info->display_name);
        xfce_message_dialog(NULL, _("File Error"), GTK_STOCK_DIALOG_ERROR,
                            primary, error->message,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(primary);
    }
}

static void
xfdesktop_file_icon_drag_job_finished(ThunarVfsJob *job,
                                      gpointer user_data)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(user_data);
    XfdesktopFileIcon *src_file_icon = g_object_get_data(G_OBJECT(job),
                                                         "--xfdesktop-src-file-icon");
    
    if(g_list_find(file_icon->priv->active_jobs, job)) {
        file_icon->priv->active_jobs = g_list_remove(file_icon->priv->active_jobs,
                                                     job);
    } else
        g_critical("ThunarVfsJob 0x%p not found in active jobs list", job);
    
    if(g_list_find(src_file_icon->priv->active_jobs, job)) {
        src_file_icon->priv->active_jobs = g_list_remove(src_file_icon->priv->active_jobs,
                                                         job);
    } else
        g_critical("ThunarVfsJob 0x%p not found in active jobs list", job);
    
    /* yes, twice is correct */
    g_object_unref(G_OBJECT(job));
    g_object_unref(G_OBJECT(job));
}

static XfdesktopIconDragResult
xfdesktop_file_icon_do_drop_dest(XfdesktopIcon *icon,
                                 XfdesktopIcon *src_icon,
                                 GdkDragAction action)
{
    XfdesktopFileIcon *file_icon = XFDESKTOP_FILE_ICON(icon);
    XfdesktopFileIcon *src_file_icon = XFDESKTOP_FILE_ICON(src_icon);
    
    DBG("entering");
    
    g_return_val_if_fail(file_icon && src_file_icon,
                         XFDESKTOP_ICON_DRAG_FAILED);
    g_return_val_if_fail(xfdesktop_file_icon_is_drop_dest(icon),
                         XFDESKTOP_ICON_DRAG_FAILED);
    
    if(file_icon->priv->info->flags & THUNAR_VFS_FILE_FLAGS_EXECUTABLE) {
        GList *path_list = g_list_prepend(NULL, src_file_icon->priv->info->path);
        GError *error = NULL;
        gboolean succeeded;
        
        succeeded = thunar_vfs_info_execute(file_icon->priv->info,
                                            file_icon->priv->gscreen,
                                            path_list,
                                            xfce_get_homedir(),
                                            &error);
        g_list_free(path_list);
        
        if(!succeeded) {
            gchar *primary = g_strdup_printf(_("Failed to run \"%s\":"),
                                             file_icon->priv->info->display_name);
            xfce_message_dialog(NULL, _("Run Error"), GTK_STOCK_DIALOG_ERROR,
                                primary, error->message,
                                GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
            g_free(primary);
            g_error_free(error);
            
            return XFDESKTOP_ICON_DRAG_FAILED;
        }
        
        return XFDESKTOP_ICON_DRAG_SUCCEEDED_NO_ACTION;
    } else {
        ThunarVfsJob *job = NULL;
        const gchar *name;
        ThunarVfsPath *dest_path;
        
        name = thunar_vfs_path_get_name(src_file_icon->priv->info->path);
        g_return_val_if_fail(name, XFDESKTOP_ICON_DRAG_FAILED);
        
        dest_path = thunar_vfs_path_relative(file_icon->priv->info->path,
                                             name);
        g_return_val_if_fail(dest_path, XFDESKTOP_ICON_DRAG_FAILED);
        
        switch(action) {
            case GDK_ACTION_MOVE:
                DBG("doing move");
                job = thunar_vfs_move_file(src_file_icon->priv->info->path,
                                           dest_path, NULL);
                break;
            
            case GDK_ACTION_COPY:
                DBG("doing copy");
                job = thunar_vfs_copy_file(src_file_icon->priv->info->path,
                                           dest_path, NULL);
                break;
            
            case GDK_ACTION_LINK:
                DBG("doing link");
                job = thunar_vfs_link_file(src_file_icon->priv->info->path,
                                           dest_path, NULL);
                break;
            
            default:
                g_warning("Unsupported drag action: %d", action);
        }
        
        thunar_vfs_path_unref(dest_path);
        
        if(job) {
            DBG("got job, action initiated");
            
            g_object_set_data(G_OBJECT(job), "--xfdesktop-file-icon-callback",
                              G_CALLBACK(xfdesktop_file_icon_drag_job_finished));
            g_object_set_data(G_OBJECT(job), "--xfdesktop-file-icon-data", icon);
            file_icon->priv->active_jobs = g_list_prepend(file_icon->priv->active_jobs,
                                                          job);
            src_file_icon->priv->active_jobs = g_list_prepend(src_file_icon->priv->active_jobs,
                                                              job);
            
            g_object_set_data(G_OBJECT(job), "--xfdesktop-src-file-icon",
                              src_file_icon);
            g_object_set_data(G_OBJECT(job), "--xfdesktop-file-icon-action",
                              GINT_TO_POINTER(action));
            g_signal_connect(G_OBJECT(job), "error",
                             G_CALLBACK(xfdesktop_file_icon_drag_job_error),
                             file_icon);
            g_signal_connect(G_OBJECT(job), "finished",
                             G_CALLBACK(xfdesktop_file_icon_drag_job_finished),
                             file_icon);
            
            g_object_ref(G_OBJECT(job));
            
            return XFDESKTOP_ICON_DRAG_SUCCEEDED_NO_ACTION;
        } else
            return XFDESKTOP_ICON_DRAG_FAILED;
    }
    
    return XFDESKTOP_ICON_DRAG_FAILED;
}

static void
xfdesktop_delete_file_error(ThunarVfsJob *job,
                            GError *error,
                            gpointer user_data)
{
    XfdesktopFileIcon *icon = XFDESKTOP_FILE_ICON(user_data);
    gchar *primary = g_strdup_printf("There was an error deleting \"%s\":", icon->priv->info->display_name);
                                     
    xfce_message_dialog(NULL, _("Error"), GTK_STOCK_DIALOG_ERROR, primary,
                        error->message, GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT,
                        NULL);
    
    g_free(primary);
}

static void
xfdesktop_delete_file_finished(ThunarVfsJob *job,
                               gpointer user_data)
{
    XfdesktopFileIcon *icon = XFDESKTOP_FILE_ICON(user_data);
    
    if(g_list_find(icon->priv->active_jobs, job))
        icon->priv->active_jobs = g_list_remove(icon->priv->active_jobs, job);
    else
        g_critical("ThunarVfsJob 0x%p not found in active jobs list", job);
    
    g_object_unref(G_OBJECT(job));
}

void
xfdesktop_file_icon_delete_file(XfdesktopFileIcon *icon)
{
    ThunarVfsJob *job;
    
    job = thunar_vfs_unlink_file(icon->priv->info->path, NULL);
    
    g_object_set_data(G_OBJECT(job), "--xfdesktop-file-icon-callback",
                      G_CALLBACK(xfdesktop_delete_file_finished));
    g_object_set_data(G_OBJECT(job), "--xfdesktop-file-icon-data", icon);
    icon->priv->active_jobs = g_list_prepend(icon->priv->active_jobs, job);
    
    g_signal_connect(G_OBJECT(job), "error",
                     G_CALLBACK(xfdesktop_delete_file_error), icon);
    g_signal_connect(G_OBJECT(job), "finished",
                     G_CALLBACK(xfdesktop_delete_file_finished), icon);
}

gboolean
xfdesktop_file_icon_rename_file(XfdesktopFileIcon *icon,
                                const gchar *new_name)
{
    GError *error = NULL;
    
    g_return_val_if_fail(XFDESKTOP_IS_FILE_ICON(icon) && new_name && *new_name,
                         FALSE);
        
    if(!thunar_vfs_info_rename(icon->priv->info, new_name, &error)) {
        gchar *primary = g_strdup_printf(_("Failed to rename \"%s\" to \"%s\":"),
                                         icon->priv->info->display_name,
                                         new_name);
        xfce_message_dialog(NULL, _("Error"), GTK_STOCK_DIALOG_ERROR,
                            primary, error->message,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(primary);
        g_error_free(error);
        
        return FALSE;
    }
    
    return TRUE;
}

G_CONST_RETURN ThunarVfsInfo *
xfdesktop_file_icon_peek_info(XfdesktopFileIcon *icon)
{
    g_return_val_if_fail(XFDESKTOP_IS_FILE_ICON(icon), NULL);
    return icon->priv->info;
}

void
xfdesktop_file_icon_update_info(XfdesktopFileIcon *icon,
                                ThunarVfsInfo *info)
{
    g_return_if_fail(XFDESKTOP_IS_ICON(icon) && info);
    
    thunar_vfs_info_unref(icon->priv->info);
    icon->priv->info = thunar_vfs_info_ref(info);
    
    xfdesktop_icon_label_changed(XFDESKTOP_ICON(icon));
}

GList *
xfdesktop_file_icon_list_to_path_list(GList *icon_list)
{
    GList *path_list = NULL, *l;
    
    for(l = icon_list; l; l = l->next) {
        path_list = g_list_prepend(path_list,
                                   thunar_vfs_path_ref(XFDESKTOP_FILE_ICON(l->data)->priv->info->path));
    }
    
    return g_list_reverse(path_list);
}
