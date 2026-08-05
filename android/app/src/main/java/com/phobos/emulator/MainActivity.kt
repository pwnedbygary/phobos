package com.phobos.emulator

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.collectAsState
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import coil.Coil
import coil.ImageLoader
import coil.decode.SvgDecoder
import com.phobos.emulator.data.SettingsStore
import com.phobos.emulator.ui.MainScaffold
import com.phobos.emulator.ui.MainViewModel
import com.phobos.emulator.ui.theme.PhobosTheme

class MainActivity : ComponentActivity() {

    private lateinit var settingsStore: SettingsStore
    private lateinit var viewModel: MainViewModel

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val imageLoader = ImageLoader.Builder(this)
            .components {
                add(SvgDecoder.Factory())
            }
            .build()
        Coil.setImageLoader(imageLoader)
        
        settingsStore = SettingsStore(this)
        viewModel = ViewModelProvider(this, object : ViewModelProvider.Factory {
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return MainViewModel(applicationContext, settingsStore) as T
            }
        })[MainViewModel::class.java]

        lifecycle.addObserver(viewModel)

        setContent {
            val settingsState = viewModel.settings.collectAsState()
            val settings = settingsState.value
            
            PhobosTheme(themeMode = settings.themeMode) {
                MainScaffold(viewModel = viewModel)
            }
        }
    }
}
