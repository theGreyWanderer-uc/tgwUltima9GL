import { useId, useState, type CSSProperties, type PointerEvent } from 'react';
import { Button as PixelButton } from './vendor/8bitcn/components/ui/8bit/button';

interface ScreenshotScene {
  id: number;
  title: string;
  description: string;
}

const screenshotScenes: ScreenshotScene[] = [
  { id: 0, title: 'Graveyard mist', description: 'A graveyard at night, with green mist between the headstones.' },
  { id: 1, title: 'Lantern light', description: 'A carried lantern among broken stone columns.' },
  { id: 2, title: 'Fire & stone', description: 'Flaming braziers above a horned stone statue.' },
  { id: 3, title: 'Standing stones', description: 'Lanterns, standing stones and an altar beneath the evening sky.' },
  { id: 5, title: 'Cathedral', description: 'A tall golden ankh in a stone chamber.' },
  { id: 8, title: 'Magic Circle', description: 'A circular altar surrounded by candles and stone walls.' },
  { id: 10, title: 'The great hall', description: 'A red carpet leading through a stone hall beneath hanging banners.' },
  { id: 11, title: 'Burning Column', description: 'Rocky ledges beside a lava pool and falling lava.' },
  { id: 12, title: 'Ghost Ship', description: 'A moon above dark water and wooden harbour structures.' },
  { id: 14, title: 'Faces in the dark', description: 'Carved stone faces beside lava in a dark cavern.' },
  { id: 17, title: 'Shrine of Valor', description: 'A stone altar and candles among dark rocks.' },
  { id: 19, title: 'Shallow water', description: 'A figure standing in blue water beside a sandy shoreline.' },
  { id: 22, title: 'Realm of Fire', description: 'Lava channels around tall stone pillars and rocky walkways.' },
];

function ScreenshotComparison({ scene }: { scene: ScreenshotScene }) {
  const [beforePercent, setBeforePercent] = useState(50);
  const descriptionId = useId();
  const beforeUrl = `${import.meta.env.BASE_URL}screenshots/u9-${scene.id}-before.png`;
  const afterUrl = `${import.meta.env.BASE_URL}screenshots/u9-${scene.id}-after.png`;
  const comparisonStyle: CSSProperties & { '--comparison-position': string } = {
    '--comparison-position': `${beforePercent}%`,
  };

  function moveComparisonDivider(event: PointerEvent<HTMLInputElement>) {
    const bounds = event.currentTarget.getBoundingClientRect();
    if (bounds.width === 0) return;
    setBeforePercent(Math.round(Math.max(0, Math.min(100, (event.clientX - bounds.left) / bounds.width * 100))));
  }

  return (
    <figure className="screenshot-comparison">
      <div className="comparison-viewport" style={comparisonStyle}>
        <img className="comparison-image" src={afterUrl} width="1920" height="1080" loading="lazy" decoding="async" draggable={false} alt={`${scene.title}, after: ${scene.description}`} />
        <img className="comparison-image comparison-before" src={beforeUrl} width="1920" height="1080" loading="lazy" decoding="async" draggable={false} alt={`${scene.title}, before: ${scene.description}`} />
        <span className="comparison-label comparison-label-before retro" aria-hidden="true" hidden={beforePercent === 0}>Before</span>
        <span className="comparison-label comparison-label-after retro" aria-hidden="true" hidden={beforePercent === 100}>After</span>
        <div className="comparison-divider" aria-hidden="true"><span className="comparison-grip">‹ ›</span></div>
        <input
          className="comparison-range"
          type="range"
          min="0"
          max="100"
          step="1"
          value={beforePercent}
          aria-label={`${scene.title}: before and after comparison`}
          aria-valuetext={`${beforePercent}% before, ${100 - beforePercent}% after`}
          aria-describedby={descriptionId}
          onChange={event => setBeforePercent(event.currentTarget.valueAsNumber)}
          onPointerDown={event => {
            if (event.button !== 0 || !event.isPrimary) return;
            event.preventDefault();
            event.currentTarget.focus({ preventScroll: true });
            event.currentTarget.setPointerCapture(event.pointerId);
            moveComparisonDivider(event);
          }}
          onPointerMove={event => {
            if (event.currentTarget.hasPointerCapture(event.pointerId)) moveComparisonDivider(event);
          }}
          onPointerUp={event => {
            if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
          }}
        />
      </div>
      <figcaption className="comparison-caption">
        <div><h3>{scene.title}</h3><p id={descriptionId}>Drag left or right to compare. You can also focus the image and use the arrow keys.</p></div>
        <div className="comparison-originals"><a href={beforeUrl} target="_blank" rel="noopener">Before (full size)</a><a href={afterUrl} target="_blank" rel="noopener">After (full size)</a></div>
      </figcaption>
    </figure>
  );
}

export function ScreenshotComparisons() {
  const [scene, setScene] = useState<ScreenshotScene>(screenshotScenes[0]);

  return (
    <section id="screenshots" className="screenshot-section site-section site-wrap" aria-labelledby="screenshots-title">
      <p className="eyebrow retro">02 / BEFORE &amp; AFTER</p>
      <h2 id="screenshots-title" className="retro section-title">Experience the difference.</h2>
      <p className="section-intro">Choose a scene, then slide between the before and after screenshots.</p>
      <div className="comparison-scenes" role="group" aria-label="Choose a screenshot comparison">
        {screenshotScenes.map((option, index) => (
          <PixelButton key={option.id} font="normal" variant={scene.id === option.id ? 'default' : 'outline'} aria-pressed={scene.id === option.id} onClick={() => setScene(option)}>
            <span className="scene-number" aria-hidden="true">{String(index + 1).padStart(2, '0')}</span>{option.title}
          </PixelButton>
        ))}
      </div>
      <ScreenshotComparison key={scene.id} scene={scene} />
    </section>
  );
}
