// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

import android.app.Application;
import android.content.Context;
import java.io.File;

/** App identity/storage boundary; deliberately keeps upstream's JNI namespace. */
public final class QuestPlatform extends Application {
    private static Context context;
    @Override public void onCreate() {
        super.onCreate();
        context = getApplicationContext();
    }
    static File userDataDir() {
        if (context == null) throw new IllegalStateException("Quest application is not initialized");
        return new File(context.getFilesDir(), "UserData/ZeroHour");
    }
}
