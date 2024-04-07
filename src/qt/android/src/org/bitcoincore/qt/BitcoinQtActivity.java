package org.bitbicore.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class BitbiQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File bitbiDir = new File(getFilesDir().getAbsolutePath() + "/.bitbi");
        if (!bitbiDir.exists()) {
            bitbiDir.mkdir();
        }

        super.onCreate(savedInstanceState);
    }
}
