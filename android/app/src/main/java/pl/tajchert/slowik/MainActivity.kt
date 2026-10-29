package pl.tajchert.slowik

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.Text
import pl.tajchert.slowik.core.designsystem.theme.SlowikTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { SlowikTheme { Text("Slowik") } }
    }
}
