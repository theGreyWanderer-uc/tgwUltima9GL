# U9GL website

A static React/TypeScript site built with Vite and Tailwind CSS, using selected
8bitcn/ui components, plus a before/after gallery of supplied gameplay screenshots.
It has no server-side features or extracted game resources. Requires
Node.js 22.13 or newer and npm.

From this folder:

```powershell
npm ci
npm run dev
```

Open <http://127.0.0.1:4173>. Edits reload automatically. Stop with Ctrl+C.

To check the production files, including the GitHub Pages project path:

```powershell
npm run build
npm run preview:pages
```

Open <http://127.0.0.1:4174/tgwUltima9GL/>. The build goes into `dist/`, which
is ignored by Git. Relative asset URLs support both project paths and domain roots.
Use `npm run preview` instead to preview at the domain root.

Edit `src/u9-project-site.tsx` for content and `src/site.css` for appearance. The release and source
links target the project's GitHub repository; they become accessible to visitors
when that repository and its releases are public.

`src/vendor/8bitcn/` holds the selected pixel buttons, badges, cards, drawers, popovers and Hero1 layout,
with their base components and MIT licence. Its README records the origin and the
small adaptations. Press Start 2P is self-hosted through Fontsource. Both the
8bitcn MIT and font OFL notices are in `public/licenses/`, copied into the build and
linked from the footer. The export preserves those notices and vendored files
verbatim. Additional notices for Vaul, Radix Popover and the animation stylesheet
are also in `public/licenses/`. The rest of U9GL keeps its own licence.

The theme switch uses browser storage when available. Section navigation is by
anchors, so no server-side routing or rewrite rules are needed. The build runs a
strict TypeScript check before producing `dist/`.

## Feature guides

The three project cards open `src/feature-drawer.tsx`: a renderer overview, the
launcher's Graphics options, and the launcher's Fixes entries. Each topic opens a
detail popover inside its drawer. Guides include search and support touch, keyboard,
Escape dismissal and returning focus to the trigger. The content is informational;
game settings are changed in the launcher.

`src/launcher-options.json` is a snapshot of the launcher catalogue's player-facing
titles, descriptions and normal-page choices. Refresh it when the catalogue changes.
It includes no diagnostics or internal settings keys. Defaults are not shown, since
release profiles and players' saved settings can differ. Fixes are described as
remaining enabled, in line with the launcher's Fixes page.

## Screenshot comparisons

`src/screenshot-comparisons.tsx` lists 13 scenes and displays one pair at a time.
Drag across the image with a mouse or touch, or focus the comparison and use the
arrow keys, Home and End. Selecting a scene resets the split to the middle.
Both images keep their full framing as the divider moves.

The 26 original 1920×1080 PNGs live in `public/screenshots/`, named
`u9-<id>-before.png` / `u9-<id>-after.png`. Capture IDs are not consecutive;
the scene list records which pairs are shown and gives each a descriptive label.
They are served unchanged and copied verbatim by the public export. Only the active
pair is rendered, with lazy image loading; the full-size links open the originals.
These are gameplay captures supplied for the website, rather than extracted assets.

`src/dashboard-gallery.tsx` separately displays the Player, World, Textures and Events
dashboard captures as ordinary images. Their original PNGs (`dashboardPlayer.png`, `dashboardWorld.png`,
`dashboardTextures.png`, `dashboardEvents.png`) also live in `public/screenshots/`
and export unchanged. Each image links to its full-size original; these do not use
the comparison slider.

## GitHub Pages

The repository's `.github/workflows/pages.yml` builds this folder for pull requests
and changes on `main`. Deployment runs only on `main` in the public
`theGreyWanderer-uc/tgwUltima9GL` repository, and only when it is public.

After making the repository public, set **Settings → Pages → Build and deployment →
Source → GitHub Actions**, then run the **website** workflow manually. Later website
changes on `main` deploy automatically. The default project URL is
<https://theGreyWanderer-uc.github.io/tgwUltima9GL/>.

GitHub Free supports Pages for public repositories. Local development works regardless
of repository visibility. See [GitHub Pages availability](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages)
and [Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
