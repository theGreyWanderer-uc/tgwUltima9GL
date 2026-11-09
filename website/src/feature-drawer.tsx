import { useId, useState } from 'react';
import { Button as PixelButton } from './vendor/8bitcn/components/ui/8bit/button';
import {
  Drawer,
  DrawerClose,
  DrawerContent,
  DrawerDescription,
  DrawerFooter,
  DrawerHeader,
  DrawerTitle,
  DrawerTrigger,
} from './vendor/8bitcn/components/ui/8bit/drawer';
import { Popover, PopoverContent, PopoverTrigger } from './vendor/8bitcn/components/ui/8bit/popover';
import launcherOptions from './launcher-options.json';

interface FeatureDetailItem {
  title: string;
  description: string;
  choices?: { label: string; note: string }[];
}

const rendererDetails: FeatureDetailItem[] = [
  {
    title: 'A native fourth backend',
    description: 'U9GL adds an OpenGL backend alongside the game’s original DirectX 6, DirectX 7 and Glide renderers. It draws the scenes prepared by the game’s engine; the world and gameplay still belong to Ultima IX.',
  },
  {
    title: 'Lighting and atmosphere',
    description: 'Per-pixel lamp light, lamp shadows, sun and moon shadows, glow and fog give the world a different look. The launcher’s Graphics page lets you choose the lighting and shadow options you want.',
  },
  {
    title: 'Finishing the picture',
    description: 'Enhanced graphics draws the frame offscreen and finishes it in another pass. Lamp light, shadows, the night look and game brightness use this path. Optional 2× supersampling draws the world at twice the width and height, then averages it down for steadier edges, at a cost in speed.',
  },
  {
    title: 'Textures and replacements',
    description: 'The game’s original textures load at full size. The optional replacement-texture system can draw images from a textures folder beside U9GL.exe, at their own size. Each replacement is named for the texture it replaces.',
  },
  {
    title: 'Built for the GOG release',
    description: 'U9GL targets the GOG release of Ultima IX: Ascension. The launcher checks the game files before installation. You need your own copy of the game; U9GL supplies the renderer, launcher and fixes.',
  },
];

const featureGuides = {
  renderer: {
    title: 'The OpenGL renderer',
    button: 'About the renderer',
    description: 'A closer look at how U9GL draws Britannia. Choose a topic for the details.',
    items: rendererDetails,
    footer: 'For graphics choices and their effects, open Graphics settings.',
  },
  graphics: {
    title: 'Graphics settings',
    button: 'Graphics',
    description: 'The options on the launcher’s Graphics page. Choose an option to see what it does and the choices available.',
    items: launcherOptions.graphics,
    footer: 'Change these options in the U9GL launcher. Changes apply the next time the game starts.',
  },
  game: {
    title: 'Game settings',
    button: 'Game',
    description: 'The options on the launcher’s Game page: how the game plays and behaves. Choose an option to see what it does and the choices available.',
    items: launcherOptions.game,
    footer: 'Change these options in the U9GL launcher. Changes apply the next time the game starts.',
  },
  fixes: {
    title: 'Fixes for the original game',
    button: 'Fixes',
    description: 'The fixes on the launcher’s Fixes page. They stay enabled: turning one off would bring back its original crash or defect.',
    items: launcherOptions.fixes,
    footer: 'These descriptions follow the launcher’s Fixes page. The website does not change your game settings.',
  },
};

function FeatureDetail({ item, portalContainer }: { item: FeatureDetailItem; portalContainer: HTMLElement | null }) {
  const [detailOpen, setDetailOpen] = useState(false);

  return (
    <Popover open={detailOpen} onOpenChange={setDetailOpen}>
      <PopoverTrigger asChild>
        <button type="button" className="feature-detail-trigger" aria-label={`Read about ${item.title}`}>
          <span>{item.title}</span><span className="feature-detail-hint" aria-hidden="true">Details +</span>
        </button>
      </PopoverTrigger>
      <PopoverContent font="normal" className="feature-popover" container={portalContainer} align="start" sideOffset={10} collisionPadding={24} aria-label={`${item.title} details`} data-vaul-no-drag>
        <h3>{item.title}</h3>
        <p>{item.description}</p>
        {item.choices && (
          <div className="feature-choice-list">
            <h4>In the launcher</h4>
            <ul>{item.choices.map(choice => <li key={choice.label}><strong>{choice.label}</strong>{choice.note && <span> — {choice.note}</span>}</li>)}</ul>
          </div>
        )}
        <PixelButton font="normal" variant="outline" onClick={() => setDetailOpen(false)}>Close details</PixelButton>
      </PopoverContent>
    </Popover>
  );
}

export function FeatureDrawer({ kind }: { kind: keyof typeof featureGuides }) {
  const guide = featureGuides[kind];
  const [drawerNode, setDrawerNode] = useState<HTMLDivElement | null>(null);
  const [drawerQuery, setDrawerQuery] = useState('');
  const guideSearchId = useId();
  const query = drawerQuery.trim().toLowerCase();
  const visibleItems = guide.items.filter(item => `${item.title} ${item.description}`.toLowerCase().includes(query));

  return (
    <Drawer direction="right" autoFocus onOpenChange={open => { if (!open) setDrawerQuery(''); }}>
      <DrawerTrigger asChild><PixelButton font="normal" className="feature-guide-button">{guide.button}</PixelButton></DrawerTrigger>
      <DrawerContent side="right" className="feature-drawer" ref={setDrawerNode}>
        <DrawerHeader>
          <div className="feature-drawer-heading"><DrawerTitle>{guide.title}</DrawerTitle><DrawerClose asChild><PixelButton font="normal" variant="outline" aria-label={`Close ${guide.title}`}>Close</PixelButton></DrawerClose></div>
          <DrawerDescription>{guide.description}</DrawerDescription>
        </DrawerHeader>
        <div className="feature-drawer-scroll" data-vaul-no-drag>
          <label className="feature-search-label" htmlFor={guideSearchId}>Find a topic</label>
          <input id={guideSearchId} className="feature-search" type="search" placeholder="Search by name or description" value={drawerQuery} onChange={event => setDrawerQuery(event.currentTarget.value)} />
          <p className="feature-result-count" aria-live="polite">{visibleItems.length} of {guide.items.length} {kind === 'fixes' ? 'fixes' : kind === 'renderer' ? 'topics' : 'options'}</p>
          <ul className="feature-detail-list">{visibleItems.map(item => <li key={item.title}><FeatureDetail item={item} portalContainer={drawerNode} /></li>)}</ul>
          {visibleItems.length === 0 && <p className="feature-empty">No matches. Try another word.</p>}
        </div>
        <DrawerFooter><p>{guide.footer}</p></DrawerFooter>
      </DrawerContent>
    </Drawer>
  );
}
