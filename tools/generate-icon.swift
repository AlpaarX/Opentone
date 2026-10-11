import AppKit
let size = NSSize(width: 1024, height: 1024)
let image = NSImage(size: size)
image.lockFocus()
NSColor.white.setFill()
NSBezierPath(roundedRect: NSRect(origin: .zero, size: size), xRadius: 192, yRadius: 192).fill()
let ink = NSColor(calibratedRed: 23/255, green: 27/255, blue: 32/255, alpha: 1)
ink.setStroke()
let ring = NSBezierPath(ovalIn: NSRect(x: 250, y: 282, width: 442, height: 540))
ring.lineWidth = 86
ring.stroke()
let stem = NSBezierPath()
stem.move(to: NSPoint(x: 716, y: 222))
stem.line(to: NSPoint(x: 716, y: 447))
stem.lineWidth = 58
stem.lineCapStyle = .round
NSColor.white.setStroke()
stem.stroke()
stem.lineWidth = 28
ink.setStroke()
stem.stroke()
let flag = NSBezierPath()
flag.move(to: NSPoint(x: 716, y: 447))
flag.curve(to: NSPoint(x: 808, y: 328), controlPoint1: NSPoint(x: 721, y: 389), controlPoint2: NSPoint(x: 818, y: 389))
flag.lineWidth = 28
flag.lineCapStyle = .round
flag.stroke()
let head = NSBezierPath(ovalIn: NSRect(x: -65, y: -44, width: 130, height: 88))
let transform = AffineTransform(translationByX: 671, byY: 214)
var rotated = transform
rotated.rotate(byDegrees: 22)
head.transform(using: rotated)
ink.setFill()
head.fill()
head.lineWidth = 9
NSColor.white.setStroke()
head.stroke()
image.unlockFocus()
let rep = NSBitmapImageRep(data: image.tiffRepresentation!)!
try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
