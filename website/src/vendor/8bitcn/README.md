# Selected 8bitcn/ui components

Copied from [TheOrcDev/8bitcn-ui](https://github.com/TheOrcDev/8bitcn-ui), local
source checkout at commit `56066c038165223b8505a344309279ae27757a81`.
The MIT licence is retained in `LICENSE.md` and served at
`public/licenses/8bitcn-LICENSE.txt` in the website source.

Included: pixel buttons, badges, cards, drawers and popovers, their base UI components,
the retro stylesheet, and the Hero1 layout. These files remain verbatim during public export,
including notices and comments. U9GL-specific composition stays outside this folder.

Adaptations for a static Vite site:

- Hero1 uses ordinary anchors instead of `next/link`; its scanline style lives in
  the site CSS and its typography and spacing are adjusted for small screens.
- The base badge uses the already-required `radix-ui` Slot export.
- `lib/utils.ts` only exports `cn`, without the upstream website URL helper.
- The Google Fonts import was removed from `retro.css`; the entry point imports
  the self-hosted Press Start 2P font, whose OFL notice ships with the site.
- PopoverContent accepts a portal container, forwarded to the base popover's Portal.
  Detail popovers are mounted inside their drawer so focus and dismissal share the
  drawer's modal boundary.

No upstream application, API routes, database, payments or environment files are
part of this selection.
