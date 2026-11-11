# MG90S and KPower M0090 servo interfaces

Owner report, 2026-11-10: the build uses MG90S servos with two spline sizes.
This is a build requirement, not a measured or physically verified specification.
Do not infer spline dimensions or interchangeability from the MG90S label.

The owner also uses the [KPower M0090 from RobotShop](https://uk.robotshop.com/products/kpower-9g-analog-metal-gear-rc-servo-m0090).
The listing checked on 2026-11-10 gives overall size 22.4 × 12.5 × 22.8 mm
(L × W × H), but no spline dimensions or tooth count in its text. These are
supplier-listed overall dimensions, not a verified shaft/mounting drawing.
Treat M0090 as a separate sample; do not equate it with either MG90S variant.
The [V1 sizing coupon](SIZING-COUPON.md) provides diameter screening trials.

| Parameter | Variant A | Variant B |
|---|---|---|
| Supplier / product link | Pending | Pending |
| Spline outside diameter | Pending | Pending |
| Spline tooth count | Pending | Pending |
| Exposed spline height | Pending | Pending |
| Retaining screw specification | Pending | Pending |
| Supplied horn style and dimensions | Pending | Pending |
| Servo roles using this variant | Pending | Pending |

A/B are temporary identifiers; neither is assigned a size yet.

## Design decision still needed

Determine whether the robot uses the supplied screw-on horns attached to printed
parts, or printed sockets attached directly to the splines. These require
different CAD interfaces. Preserve the non-TorqueStar requirement in either case.
Do not create a single generic MG90S socket with guessed dimensions.

For supplied horns, record arm width/thickness, center-to-hole distances, hole
diameters and the seated horn height relative to the servo mounting tabs.
For direct spline sockets, both spline variants need measured geometry and a
fit-test coupon before incorporating the socket into a large printed part.

Both routes also need case dimensions, mounting-tab hole spacing, shaft-axis
location and cable exit clearance. Dimensions must carry their source: owner
measurement, supplier drawing, or explicit design assumption.

No dimensions, horn fit, servo motion or physical checks have been verified here.
