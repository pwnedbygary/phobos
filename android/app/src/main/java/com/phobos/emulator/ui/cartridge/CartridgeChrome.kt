package com.phobos.emulator.ui.cartridge

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BatteryFull
import androidx.compose.material.icons.rounded.Cloud
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import coil.compose.AsyncImage
import com.phobos.emulator.R
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.util.gameTitle
import kotlinx.coroutines.delay
import java.io.File
import android.text.format.DateFormat
import java.time.LocalTime
import java.time.format.DateTimeFormatter

/** The wordmark, clock and artwork line across the top of the room. */
@Composable
fun CartridgeTopBar(artwork: Int?, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Image(
            painter = painterResource(R.drawable.phobos_logo),
            contentDescription = null,
            modifier = Modifier.height(22.dp),
        )
        Spacer(Modifier.width(8.dp))
        Text(
            "phobos",
            style = MaterialTheme.typography.titleMedium.copy(letterSpacing = (-0.2).sp),
            color = scheme.onBackground,
            fontWeight = FontWeight.SemiBold,
        )
        Spacer(Modifier.weight(1f))
        if (artwork != null) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.Cloud, contentDescription = null, modifier = Modifier.size(15.dp), tint = scheme.onBackground.copy(alpha = 0.62f))
                Spacer(Modifier.width(5.dp))
                Text(
                    "Artwork $artwork",
                    style = MaterialTheme.typography.labelMedium,
                    color = scheme.onBackground.copy(alpha = 0.62f),
                )
            }
            Spacer(Modifier.width(16.dp))
        }
        CartridgeClock()
        Spacer(Modifier.width(14.dp))
        CartridgeBattery()
    }
}

@Composable
private fun CartridgeClock() {
    val context = LocalContext.current
    var text by remember { mutableStateOf(clockText(context)) }
    LaunchedEffect(Unit) {
        while (true) {
            text = clockText(context)
            delay(15_000)
        }
    }
    Text(text, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onBackground)
}

private fun clockText(context: Context): String {
    val twentyFour = DateFormat.is24HourFormat(context)
    return DateTimeFormatter.ofPattern(if (twentyFour) "HH:mm" else "h:mm a").format(LocalTime.now())
}

@Composable
private fun CartridgeBattery() {
    val context = LocalContext.current
    var level by remember { mutableStateOf<Int?>(null) }
    var charging by remember { mutableStateOf(false) }
    DisposableEffect(context) {
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                val raw = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
                val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100).coerceAtLeast(1)
                level = if (raw >= 0) (raw * 100 / scale) else null
                charging = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1) in setOf(
                    BatteryManager.BATTERY_STATUS_CHARGING,
                    BatteryManager.BATTERY_STATUS_FULL,
                )
            }
        }
        val filter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        fun read(intent: Intent?) {
            if (intent == null) return
            val raw = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100).coerceAtLeast(1)
            level = if (raw >= 0) (raw * 100 / scale) else null
            charging = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1) in setOf(
                BatteryManager.BATTERY_STATUS_CHARGING,
                BatteryManager.BATTERY_STATUS_FULL,
            )
        }
        read(context.registerReceiver(null, filter))
        ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        onDispose { runCatching { context.unregisterReceiver(receiver) } }
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            level?.let { "$it%" } ?: "",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.62f),
        )
        Spacer(Modifier.width(5.dp))
        Icon(
            Icons.Rounded.BatteryFull,
            contentDescription = null,
            modifier = Modifier.size(18.dp),
            tint = if (charging) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onBackground.copy(alpha = 0.62f),
        )
    }
}

/** A section's small letter-spaced label with an optional icon, as Cartridge draws it above a strip. */
@Composable
fun CartridgeSectionLabel(text: String, icon: ImageVector? = null, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier = modifier.padding(horizontal = 20.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(13.dp), tint = scheme.onBackground.copy(alpha = 0.55f))
            Spacer(Modifier.width(6.dp))
        }
        Text(
            text.uppercase(),
            style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp),
            color = scheme.onBackground.copy(alpha = 0.55f),
        )
    }
}

/** One of the action pills at the foot of the room. */
@Composable
fun CartridgePill(
    icon: ImageVector,
    label: String,
    selected: Boolean = false,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    val scale by animateFloatAsState(if (selected) 1.04f else 1f, spring(), label = "cartridgePill")
    val fill = if (selected) scheme.onBackground.copy(alpha = 0.13f) else scheme.onBackground.copy(alpha = 0.055f)
    Row(
        modifier = modifier
            .graphicsLayer { scaleX = scale; scaleY = scale }
            .clip(RoundedCornerShape(percent = 50))
            .background(fill)
            .border(
                1.dp,
                if (selected) scheme.onBackground.copy(alpha = 0.22f) else scheme.onBackground.copy(alpha = 0.09f),
                RoundedCornerShape(percent = 50),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 11.dp),        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(17.dp), tint = scheme.onBackground.copy(alpha = 0.82f))
        Spacer(Modifier.width(8.dp))
        Text(label, style = MaterialTheme.typography.labelLarge, color = scheme.onBackground.copy(alpha = 0.92f))
    }
}

/** A keycap and its action, in the hints line at the bottom. */
@Composable
fun CartridgeHint(key: String, label: String, highlighted: Boolean = false) {
    val scheme = MaterialTheme.colorScheme
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .size(width = 24.dp, height = 21.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(if (highlighted) scheme.onBackground.copy(alpha = 0.18f) else scheme.onBackground.copy(alpha = 0.09f))
                .border(1.dp, scheme.onBackground.copy(alpha = 0.12f), RoundedCornerShape(6.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Text(key, style = MaterialTheme.typography.labelSmall, color = scheme.onBackground.copy(alpha = 0.85f), maxLines = 1)
        }
        Spacer(Modifier.width(6.dp))
        Text(label, style = MaterialTheme.typography.labelMedium, color = scheme.onBackground.copy(alpha = 0.62f))
    }
}

/** The hints line: the controller's own names, as Cartridge's footer prints them. */
@Composable
fun CartridgeHints(modifier: Modifier = Modifier, items: List<Pair<String, String>>) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 9.dp),
        horizontalArrangement = Arrangement.spacedBy(20.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items.forEach { (key, label) -> CartridgeHint(key, label) }
    }
}

/**
 * A game in the Continue Playing strip: its own 3D cart when it has one, else its box art, with its system
 * badge, title and age. Tapping starts it; a long press opens its menu.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun CartridgeGameTile(
    record: GameRecord,
    picture: File?,
    box: File?,
    width: Dp,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val title = gameTitle(record.key.name, record.title)
    val source = remember { MutableInteractionSource() }
    Column(
        modifier = modifier
            .width(width)
            .clip(MaterialTheme.shapes.medium)
            .combinedClickable(
                interactionSource = source,
                indication = null,
                onClick = onClick,
                onLongClick = onLongClick,
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(MaterialTheme.shapes.medium)
                .background(scheme.onBackground.copy(alpha = 0.055f))
                .border(1.dp, scheme.onBackground.copy(alpha = 0.08f), MaterialTheme.shapes.medium),
            contentAlignment = Alignment.Center,
        ) {
            val image = picture ?: box
            if (image != null) {
                AsyncImage(
                    model = image,
                    contentDescription = title,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxWidth().fillMaxHeight().padding(7.dp),
                )
            } else {
                Text(
                    factsFor(record.key.system).short,
                    style = MaterialTheme.typography.titleMedium,
                    color = scheme.onBackground.copy(alpha = 0.5f),
                )
            }
            Text(
                factsFor(record.key.system).short,
                style = MaterialTheme.typography.labelSmall,
                color = scheme.onBackground.copy(alpha = 0.8f),
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(7.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(Color.Black.copy(alpha = 0.38f))
                    .padding(horizontal = 5.dp, vertical = 2.dp),
            )
            if (record.favourite) {
                Icon(
                    Icons.Rounded.Star,
                    contentDescription = "Favorite",
                    tint = scheme.primary,
                    modifier = Modifier.align(Alignment.TopEnd).padding(7.dp).size(15.dp),
                )
            }
        }
        Text(
            title,
            style = MaterialTheme.typography.labelLarge,
            color = scheme.onBackground,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
        )
        Text(
            record.key.system,
            style = MaterialTheme.typography.labelSmall,
            color = scheme.onBackground.copy(alpha = 0.55f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth(),
            textAlign = TextAlign.Center,
        )
    }
}

/** A system's media as a 3D plate, the selected one brought forward and enlarged. */
@Composable
fun CartridgeSystemPlate(
    system: String,
    picture: File?,
    selected: Boolean,
    size: Dp,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val facts = factsFor(system)
    val scale by animateFloatAsState(if (selected) 1.25f else 1f, spring(dampingRatio = 0.72f, stiffness = 380f), label = "cartridgePlate")
    val alpha by animateFloatAsState(if (selected) 1f else 0.72f, label = "cartridgePlateAlpha")
    Column(
        modifier = modifier
            .width(size * 1.24f)
            .graphicsLayer { scaleX = scale; scaleY = scale; this.alpha = alpha }
            .clip(MaterialTheme.shapes.large)
            .clickable(onClick = onClick)
            .padding(vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(MaterialTheme.shapes.large),
            contentAlignment = Alignment.Center,
        ) {
            if (picture != null) {
                AsyncImage(
                    model = picture,
                    contentDescription = system,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxWidth().fillMaxHeight().padding(4.dp),
                )
            } else {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .aspectRatio(0.82f)
                        .clip(RoundedCornerShape(10.dp))
                        .background(facts.color.copy(alpha = 0.82f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(facts.short, style = MaterialTheme.typography.titleLarge, color = Color.White)
                }
            }
            if (selected) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .aspectRatio(1f)
                        .border(1.dp, scheme.onBackground.copy(alpha = 0.16f), MaterialTheme.shapes.large),
                )
            }
        }
    }
}

/** A full-width divider of the room. */
@Composable
fun CartridgeRule(modifier: Modifier = Modifier) {
    Box(
        modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(MaterialTheme.colorScheme.onBackground.copy(alpha = 0.07f)),
    )
}

/** The dark scrim that keeps text legible over the blurred picture. */
@Composable
fun CartridgeScrim(modifier: Modifier = Modifier) {
    Box(
        modifier.background(
            androidx.compose.ui.graphics.Brush.verticalGradient(
                0f to Color.Black.copy(alpha = 0.28f),
                0.45f to Color.Black.copy(alpha = 0.12f),
                1f to Color.Black.copy(alpha = 0.58f),
            ),
        ),
    )
}

/** Content padding for a scrolling Cartridge page, clear of the action bar and hints. */
val CartridgePagePadding = PaddingValues(horizontal = 20.dp, vertical = 14.dp)


/** Cartridge's collapsible Continue Playing handle: the strip opens under it only when asked for. */
@Composable
fun CartridgeContinueHandle(expanded: Boolean, count: Int, onClick: () -> Unit) {
    val scheme = MaterialTheme.colorScheme
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(14.dp), tint = scheme.onBackground.copy(alpha = 0.55f))
        Spacer(Modifier.width(7.dp))
        Text(
            "CONTINUE PLAYING",
            style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp),
            color = scheme.onBackground.copy(alpha = 0.55f),
        )
        if (count > 0) {
            Spacer(Modifier.width(8.dp))
            Text("$count", style = MaterialTheme.typography.labelSmall, color = scheme.onBackground.copy(alpha = 0.45f))
        }
        Spacer(Modifier.weight(1f))
        Icon(
            if (expanded) Icons.Rounded.KeyboardArrowUp else Icons.Rounded.KeyboardArrowDown,
            contentDescription = if (expanded) "Hide Continue Playing" else "Show Continue Playing",
            modifier = Modifier.size(18.dp),
            tint = scheme.onBackground.copy(alpha = 0.55f),
        )
    }
}
