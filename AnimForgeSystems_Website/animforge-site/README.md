# AnimForge Systems — website

Pure static HTML/CSS/JS. No build step, no dependencies, no server-side code.
Upload the contents of this folder to your webspace root and it's live.

## Structure

```
index.html                          Homepage (live DLS IK hero)
tools.html                          Product pages (DLS IK, RBF, Ragdoll, WarpViz)
lab.html                            Prani lab page
about.html                          About + contact
blog/index.html                     Writing index
blog/pac-vs-physicsblendweight.html Flagship starter post
assets/css/style.css                All styling (design tokens at the top)
assets/js/ik-hero.js                Hero: real 2D damped least-squares IK solver
assets/js/site.js                   Mobile nav toggle
```

## Deploying

1. FTP/SFTP the folder contents to the webspace document root (often `public_html/` or `htdocs/`).
2. Done. Everything is relative-pathed; it also works from a subdirectory.

## Before launch — checklist

- [ ] Replace `contact@animforgesystems.com` with your real address (search-and-replace, appears on every page).
- [ ] Verify the LinkedIn and GitHub URLs in the contact sections.
- [ ] Review the blog post technical content against your engine version.
- [ ] Add a favicon (`favicon.ico` in the root, or a `<link rel="icon">` tag).
- [ ] Optional: add Open Graph tags + a share image once you have a good screenshot of the IK hero.

## Adding a YouTube video

Each tool section on `tools.html` has a `.video-frame` placeholder with the embed
snippet in an HTML comment right above it. Replace the placeholder `<span>` with:

```html
<iframe src="https://www.youtube.com/embed/VIDEO_ID" title="Demo"
        allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture"
        allowfullscreen></iframe>
```

## Adding a blog post

1. Copy `blog/pac-vs-physicsblendweight.html` to `blog/your-slug.html`.
2. Replace title, meta description, and article content.
3. Add an entry to the list in `blog/index.html` (newest first — there's a
   commented template in the file) and optionally to the Writing section on `index.html`.

## Design tokens

All colors and fonts are CSS custom properties at the top of `assets/css/style.css`:
viewport dark `#14181D`, ember orange `#F2762E` (primary accent), spline cyan
`#5FD4E8` (secondary), with IBM Plex Sans/Mono + Chakra Petch.

## The hero solver

`assets/js/ik-hero.js` runs an actual 2D damped least-squares solve per frame:
`dθ = Jᵀ(JJᵀ + λ²I)⁻¹ e` with the 2×2 inverse closed-form. Chain follows the
cursor inside the hero, otherwise a Lissajous idle path. `prefers-reduced-motion`
renders a single static solved pose instead. Tune `NUM_BONES`, `LAMBDA`,
`ITERATIONS` at the top of the file.
