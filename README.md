# Reforger-Left-Handed-Weapons
A Script for Arma Reforger to prevent jittering from guns who primarily are held with the left hand like bolt action rifles.

This is caused by the Right Hand Prop bone being glued to the right hand so any hard movements causes jittering of the weapon that look unnatural.

This script simply counter acts it and forces the gun to abide to the left hand positioning only when the animation even SAL_AttachGunToLeftHand is called.

When you call SAL_AttachGunToLeftHand it captures the current transformation of right hand prop relative to the left hand and keeps it there until you call SAL_DetachGunFromLeftHand.

Simply add the events SAL_AttachGunToLeftHand and SAL_DetachGunFromLeftHand in the animations that have jittering issues.

No credit is needed, AI helped with most of the vector logic so it's hard to say it's mine.

Steps for use:

1. Drag the SAL_LeftHandedGun into anywhere under /scripts/Game/.
2. Reload scripts using Shift+R in the world editor.
3. Add the component to your weapoon.
4. In your animation graph add the events SAL_AttachGunToLeftHand and SAL_DetachGunFromLeftHand from where you want the attachment and detachment to happen.
5. Profit?
