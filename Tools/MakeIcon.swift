// Renders the standalone app icon: the AMBER livery's chassis as a macOS
// squircle, a CRT screen carrying a phosphor waveform envelope, lamp cluster
// and rivets. Run with:
//
//     swift Tools/MakeIcon.swift Assets/icon.png
//
// The output feeds juce_add_plugin's ICON_BIG, which builds the .icns.

import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

let size = 1024
let scale = CGFloat(size) / 1024.0

func colour(_ hex: UInt32, _ alpha: CGFloat = 1.0) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xff) / 255.0,
            green: CGFloat((hex >> 8) & 0xff) / 255.0,
            blue: CGFloat(hex & 0xff) / 255.0,
            alpha: alpha)
}

// The AMBER livery.
let shellDark   = colour(0x14100e)
let shellMid    = colour(0x241d18)
let bezelHi     = colour(0x473a2f)
let bezelLo     = colour(0x0d0a08)
let screenBlack = colour(0x0a0906)
let grid        = colour(0x3a2a16)
let amber       = colour(0xffb000)
let amberBright = colour(0xffd591)
let bone        = colour(0xd8c9ae)
let phosphor    = colour(0x35e08a)
let rust        = colour(0xc4562a)

let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                    bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.scaleBy(x: scale, y: scale)

// Apple's icon grid: 824pt square centred in 1024, ~185pt corner radius.
let chassis = CGRect(x: 100, y: 100, width: 824, height: 824)
let squircle = CGPath(roundedRect: chassis, cornerWidth: 185, cornerHeight: 185, transform: nil)

// Chassis with a top-lit gradient.
ctx.saveGState()
ctx.addPath(squircle)
ctx.clip()
let shellGradient = CGGradient(colorsSpace: nil,
                               colors: [shellMid, shellDark] as CFArray,
                               locations: [0.0, 1.0])!
ctx.drawLinearGradient(shellGradient,
                       start: CGPoint(x: 512, y: 924), end: CGPoint(x: 512, y: 100),
                       options: [])
ctx.restoreGState()

// Bezel edge: catch-light along the top, shadow along the bottom.
ctx.addPath(CGPath(roundedRect: chassis.insetBy(dx: 7, dy: 7), cornerWidth: 178, cornerHeight: 178, transform: nil))
ctx.setStrokeColor(colour(0x473a2f, 0.9))
ctx.setLineWidth(14)
ctx.strokePath()

// The screen, recessed.
let screen = CGRect(x: 208, y: 300, width: 608, height: 452)

ctx.setFillColor(bezelLo)
ctx.fill(screen.insetBy(dx: -14, dy: -14))
ctx.setFillColor(screenBlack)
ctx.fill(screen)

ctx.saveGState()
ctx.clip(to: screen)

// Graticule: centre line bright, quarter rules dim.
let centreY = screen.midY
let halfH = screen.height / 2
for (fraction, alpha) in [(0.5, 0.5), (1.0, 0.3)] as [(CGFloat, CGFloat)] {
    for sign: CGFloat in [-1, 1] {
        ctx.setStrokeColor(colour(0x3a2a16, alpha))
        ctx.setLineWidth(3)
        let y = centreY + sign * fraction * halfH * 0.92
        ctx.move(to: CGPoint(x: screen.minX, y: y))
        ctx.addLine(to: CGPoint(x: screen.maxX, y: y))
        ctx.strokePath()
    }
}
ctx.setStrokeColor(colour(0x5c421f, 0.7))
ctx.setLineWidth(3)
ctx.move(to: CGPoint(x: screen.minX, y: centreY))
ctx.addLine(to: CGPoint(x: screen.maxX, y: centreY))
ctx.strokePath()

// The waveform envelope: a musical-looking amplitude contour, mirrored.
func amplitude(_ t: CGFloat) -> CGFloat {
    let a = 0.42 + 0.30 * sin(t * .pi * 2.1 + 0.6)
              + 0.18 * sin(t * .pi * 5.3 + 2.1)
              + 0.10 * sin(t * .pi * 11.0 + 4.2)
    return max(0.06, min(1.0, a)) * halfH * 0.88
}

let steps = 160
let envelope = CGMutablePath()
for i in 0...steps {
    let t = CGFloat(i) / CGFloat(steps)
    let x = screen.minX + t * screen.width
    let y = centreY + amplitude(t)
    if i == 0 { envelope.move(to: CGPoint(x: x, y: y)) }
    else { envelope.addLine(to: CGPoint(x: x, y: y)) }
}
for i in stride(from: steps, through: 0, by: -1) {
    let t = CGFloat(i) / CGFloat(steps)
    let x = screen.minX + t * screen.width
    envelope.addLine(to: CGPoint(x: x, y: centreY - amplitude(t)))
}
envelope.closeSubpath()

// Body, RMS core, then the phosphor edge: wide dim glow under a bright line.
ctx.addPath(envelope)
ctx.setFillColor(colour(0xffb000, 0.20))
ctx.fillPath()

let rms = CGMutablePath()
for i in 0...steps {
    let t = CGFloat(i) / CGFloat(steps)
    let x = screen.minX + t * screen.width
    let y = centreY + amplitude(t) * 0.45
    if i == 0 { rms.move(to: CGPoint(x: x, y: y)) } else { rms.addLine(to: CGPoint(x: x, y: y)) }
}
for i in stride(from: steps, through: 0, by: -1) {
    let t = CGFloat(i) / CGFloat(steps)
    let x = screen.minX + t * screen.width
    rms.addLine(to: CGPoint(x: x, y: centreY - amplitude(t) * 0.45))
}
rms.closeSubpath()
ctx.addPath(rms)
ctx.setFillColor(colour(0xffb000, 0.38))
ctx.fillPath()

for (width, color) in [(22.0, colour(0xffb000, 0.25)), (7.0, colour(0xffd591, 0.95))] as [(CGFloat, CGColor)] {
    ctx.addPath(envelope)
    ctx.setStrokeColor(color)
    ctx.setLineWidth(width)
    ctx.setLineJoin(.round)
    ctx.strokePath()
}

// Scanlines.
ctx.setFillColor(colour(0x000000, 0.14))
var y = screen.minY
while y < screen.maxY {
    ctx.fill(CGRect(x: screen.minX, y: y, width: screen.width, height: 3))
    y += 9
}
ctx.restoreGState()

// Silkscreen: lamp cluster above the screen, a label bar below it.
let lamps: [(CGColor, CGFloat)] = [(amber, 0.0), (phosphor, 1.0), (rust, 2.0)]
for (lampColour, index) in lamps {
    let cx = 236.0 + index * 64.0
    let cy = 812.0
    if index < 2 {
        ctx.setFillColor(lampColour.copy(alpha: 0.25)!)
        ctx.fillEllipse(in: CGRect(x: cx - 26, y: cy - 26, width: 52, height: 52))
    }
    ctx.setFillColor(index < 2 ? lampColour : lampColour.copy(alpha: 0.35)!)
    ctx.fillEllipse(in: CGRect(x: cx - 13, y: cy - 13, width: 26, height: 26))
}

// The wordmark stripe: letterspaced blocks standing in for silkscreened text.
ctx.setFillColor(colour(0xd8c9ae, 0.75))
var blockX: CGFloat = 236
for width in [46, 30, 38, 30, 46, 30, 38, 46, 30, 38] as [CGFloat] {
    ctx.fill(CGRect(x: blockX, y: 216, width: width, height: 14))
    blockX += width + 18
    if blockX > 770 { break }
}

// Rivets.
for (rx, ry) in [(160.0, 160.0), (864.0, 160.0), (160.0, 864.0), (864.0, 864.0)] {
    ctx.setFillColor(bezelLo)
    ctx.fillEllipse(in: CGRect(x: rx - 13, y: ry - 13, width: 26, height: 26))
    ctx.setStrokeColor(colour(0x473a2f, 0.8))
    ctx.setLineWidth(4)
    ctx.strokeEllipse(in: CGRect(x: rx - 10, y: ry - 10, width: 20, height: 20))
}

let image = ctx.makeImage()!
let outputPath = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "icon.png"
let url = URL(fileURLWithPath: outputPath)
let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(destination, image, nil)
CGImageDestinationFinalize(destination)
print("wrote \(outputPath)")
