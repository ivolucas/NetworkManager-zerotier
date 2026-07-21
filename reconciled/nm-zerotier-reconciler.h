/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NM_ZEROTIER_RECONCILER_H
#define NM_ZEROTIER_RECONCILER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define NM_TYPE_ZEROTIER_RECONCILER (nm_zerotier_reconciler_get_type())

G_DECLARE_FINAL_TYPE(NMZerotierReconciler, nm_zerotier_reconciler, NM, ZEROTIER_RECONCILER, GObject)

/* Connects to NetworkManager asynchronously and starts reconciling. There's
 * nothing meaningful to return synchronously (the NMClient connection itself
 * is async), so this just returns the (not-yet-connected) object; failures to
 * reach NetworkManager are logged and retried by the caller's main loop
 * staying up, same as any other NM client. */
NMZerotierReconciler *nm_zerotier_reconciler_new(void);

G_END_DECLS

#endif /* NM_ZEROTIER_RECONCILER_H */
