package com.generalsx.zerohour;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.core.content.ContextCompat;

// GeneralsX @feature Codex 10/10/2026 Six Quest destinations with the existing launcher theme.
// Material 1.12 BottomNavigationView accepts at most five items. Keep the phone bar intact.
final class QuestLauncherNavigation extends LinearLayout {
    interface DestinationListener { void open(int destination); }

    private final LinearLayout[] items;
    private final FrameLayout[] indicators;
    private final ImageView[] icons;
    private final TextView[] labels;
    private final int[] destinations;

    QuestLauncherNavigation(Context context, int[] destinations, int[] titles, int[] drawables,
                            int selected, int action, DestinationListener listener) {
        super(context);
        this.destinations = destinations;
        items = new LinearLayout[destinations.length];
        indicators = new FrameLayout[destinations.length];
        icons = new ImageView[destinations.length];
        labels = new TextView[destinations.length];
        setOrientation(HORIZONTAL);
        setBaselineAligned(false);
        setLayoutDirection(View.LAYOUT_DIRECTION_LTR);
        setBackgroundColor(UiKit.color(context, R.color.gzh_surface_container_low));
        for (int i = 0; i < destinations.length; i++) {
            final int destination = destinations[i];
            LinearLayout item = new LinearLayout(context);
            items[i] = item;
            item.setOrientation(VERTICAL);
            item.setGravity(Gravity.CENTER);
            item.setPadding(0, UiKit.dp(context, 12), 0, UiKit.dp(context, 12));
            item.setMinimumHeight(UiKit.dp(context, 80));
            item.setFocusable(true);
            item.setContentDescription(context.getString(titles[i]));
            item.setBackground(new RippleDrawable(UiKit.tint(context, R.color.gzh_ripple_primary),
                null, rounded(context, Color.WHITE)));
            FrameLayout indicator = new FrameLayout(context);
            indicators[i] = indicator;
            ImageView icon = new ImageView(context);
            icons[i] = icon;
            icon.setImageDrawable(ContextCompat.getDrawable(context, drawables[i]));
            icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            indicator.addView(icon, new FrameLayout.LayoutParams(UiKit.dp(context, 22),
                UiKit.dp(context, 22), Gravity.CENTER));
            item.addView(indicator, new LinearLayout.LayoutParams(UiKit.dp(context, 64), UiKit.dp(context, 32)));
            TextView label = new TextView(context);
            labels[i] = label;
            label.setText(titles[i]);
            label.setTextSize(12);
            label.setGravity(Gravity.CENTER);
            label.setMaxLines(2);
            label.setTextDirection(View.TEXT_DIRECTION_LOCALE);
            label.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            LinearLayout.LayoutParams textLayout = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT,
                LayoutParams.WRAP_CONTENT);
            textLayout.topMargin = UiKit.dp(context, 4);
            item.addView(label, textLayout);
            item.setOnClickListener(view -> {
                // Online opens a separate activity; retain the previous tab when returning.
                if (destination != action) select(destination);
                listener.open(destination);
            });
            addView(item, new LinearLayout.LayoutParams(0, LayoutParams.MATCH_PARENT, 1f));
        }
        select(selected);
    }

    void select(int destination) {
        Context context = getContext();
        for (int i = 0; i < destinations.length; i++) {
            boolean selected = destinations[i] == destination;
            items[i].setSelected(selected);
            int color = selected ? R.color.gzh_on_primary_container : R.color.gzh_on_surface_faint;
            icons[i].setImageTintList(UiKit.tint(context, color));
            labels[i].setTextColor(UiKit.color(context, color));
            labels[i].setTypeface(Typeface.DEFAULT, selected ? Typeface.BOLD : Typeface.NORMAL);
            indicators[i].setBackground(selected
                ? rounded(context, UiKit.color(context, R.color.gzh_primary_container)) : null);
        }
    }

    private static GradientDrawable rounded(Context context, int color) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(UiKit.dp(context, 20));
        return shape;
    }
}
