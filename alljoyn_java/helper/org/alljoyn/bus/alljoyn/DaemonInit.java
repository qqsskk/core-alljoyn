/*
 *  *    Copyright (c) Open Connectivity Foundation (OCF) and AllJoyn Open
 *    Source Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
 *    Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *     WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *     WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *     AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *     DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *     PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *     TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *     PERFORMANCE OF THIS SOFTWARE.
 */

package org.alljoyn.bus.alljoyn;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiManager;
import android.os.Process;
import android.util.Log;

/**
 * This class is to ensure that AllJoyn daemon is initialized to be ready for accepting connections from clients.
 * Before invoking Connect() to BusAttachment, the application should call PrepareDaemon()/PrepareDaemonAsync()
 * if it expects to use the bundled daemon no other daemons are available. PrepareDaemon() waits until the daemon
 * initialization is done. PrepareDaemonAsync() is the async version of PrepareDaemon().
 *
 */
public class DaemonInit {

    private final static String TAG = "DaemonInit";
    private static Context sContext;

    public static BroadcastReceiver receiver;

    public static Context getContext(){
        return sContext;
    }

    /**
     * Initialize daemon service if needed.
     * First it checks whether any daemon is running; if no daemon is running, then it starts the APK daemon if it is installed;
     * If no APK daemon is installed, then starts the bundled daemon. This is a blocking call; it blocks until the daemon is ready or
     * no daemon is found. Thus only non-UI thread is allowed to call PrepareDaemon().
     * @param context The application context
     * @return true  if the daemon is ready for connection
     *         false if no daemon is available
     */
    public static boolean PrepareDaemon(Context context) {
        Log.v(TAG, "Android version : " + android.os.Build.VERSION.SDK_INT);

        sContext = context.getApplicationContext();
        Log.v(TAG, "Saved application context");

        return true;
    }

}