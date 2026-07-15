# Bauhaus — Neo-Brutalist

## North Star: “Form Follows Function”

Bold, raw, and unapologetic. Inspired by Bauhaus and brutalist architecture. High contrast, strong geometry, deliberate imperfection.

## Colors

- **Primary (`#1a1a1a`):** Near-black for text and primary elements.
- **Accent Yellow (`#ffcc00`):** High-energy highlight for CTAs and active states.
- **Accent Red (`#e63b2e`):** Destructive actions, alerts, and bold emphasis.
- **Accent Blue (`#0055ff`):** Links and interactive elements.
- **Background (`#f5f0e8`):** Warm off-white, like aged paper.
- Use flat, solid color blocks. No gradients on surfaces.

## Typography

- **Headlines:** Space Grotesk — geometric, bold, oversized. Use display sizes aggressively (3x+ body size).
- **Body:** Inter — functional and readable.
- Scale contrast is key: headlines should feel massive relative to body text.

## Elevation

- No drop shadows. Use **thick solid borders** (2–3px, `#1a1a1a`) instead.
- Depth via **offset shadows**: solid-color block offsets (4–6px down-right in `#1a1a1a`).

## Components

- **Buttons:** Solid fill, thick border, uppercase text. Hover = color invert.
- **Cards:** Thick black borders, no border-radius or minimal. Content-dense.
- **Inputs:** Thick bottom border only. No rounded corners.

## Rules

- Never use soft shadows or glassmorphism. Keep it raw and graphic.
- Embrace asymmetric layouts and oversized type.
- Limited palette: black + one accent per section.

## Product application

- Yellow marks scan, preview, selection, and retained states.
- Blue identifies repository navigation and neutral interactive data.
- Red is reserved for released snapshots, warnings, and destructive cleanup.
- Every interactive control owns its foreground and background colors; platform theme defaults must not determine legibility.
- The application owns its complete window chrome: title, drag area, window controls, and border use the same graphic system as the content.
- Cleanup is a visible two-step batch flow: preview all scanned repositories first, then unlock batch cleanup for that exact reviewed plan.
