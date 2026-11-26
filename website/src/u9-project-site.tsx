import { Fragment, useEffect, useState } from 'react';
import { Button as PixelButton } from './vendor/8bitcn/components/ui/8bit/button';
import {
  Card as PixelCard,
  CardContent,
  CardHeader,
  CardTitle,
} from './vendor/8bitcn/components/ui/8bit/card';
import Hero1 from './vendor/8bitcn/components/ui/8bit/blocks/hero1';
import { ScreenshotComparisons } from './screenshot-comparisons';
import { DashboardGallery } from './dashboard-gallery';
import { FeatureDrawer } from './feature-drawer';

const projectUrl = 'https://github.com/theGreyWanderer-uc/tgwUltima9GL';
type SiteTheme = 'dark' | 'light';

function readSiteTheme(): SiteTheme {
  try {
    const savedTheme = localStorage.getItem('u9gl-website-theme');
    if (savedTheme === 'dark' || savedTheme === 'light') return savedTheme;
  } catch {
  }
  return matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

const features = [
  { number: '01', title: 'A fourth renderer', description: 'A modern new OpenGL renderer joins the game’s DirectX 6, DirectX 7 and Glide renderers, with improved lighting, glow, fog and shadows.', details: ['renderer'] },
  { number: '02', title: 'Your settings, together', description: 'Install, choose your display, graphics and game options and play from one launcher, which also checks your saves, the game’s files and any fan patches. Play full screen, or in a window that keeps running while you use other programs. An optional live web dashboard shows how the game is running.', details: ['game', 'display', 'graphics'] },
  { number: '03', title: 'Fixes for the original game', description: 'More than 25 fixes for crashes and faults in the original game: hostile NPCs that chase you but never attack, the crash in Lord British’s fight with Blackthorn, slow-motion animation once Windows has been on for about 25 days. They stay enabled in the launcher. Explore what each one changes.', details: ['fixes'] },
] as const;

export function U9ProjectSite() {
  const [theme, setTheme] = useState<SiteTheme>(readSiteTheme);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    document.querySelector('meta[name="theme-color"]')?.setAttribute('content', theme === 'dark' ? '#171e1c' : '#f4eedb');
    try {
      localStorage.setItem('u9gl-website-theme', theme);
    } catch {
    }
  }, [theme]);

  return (
    <>
      <a className="skip-link" href="#main">Skip to content</a>
      <header className="site-header site-wrap">
        <a className="wordmark retro" href="#" aria-label="U9GL home">U9<span>GL</span><small>ULTIMA IX / OPENGL</small></a>
        <nav aria-label="Main navigation">
          <a href="#about">The project</a>
          <a href="#screenshots">Screenshots</a>
          <a href="#dashboard">Dashboard</a>
          <a href="#install">Install</a>
          <a href={projectUrl}>Source</a>
          <PixelButton font="normal" variant="outline" className="theme-button" aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`} onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>
            {theme === 'dark' ? 'Light theme' : 'Dark theme'}
          </PixelButton>
        </nav>
      </header>

      <main id="main">
        <Hero1
          className="project-hero"
          title={'Ultima IX\nOPENGL'}
          subtitle="A FAN PROJECT FOR ULTIMA IX: ASCENSION"
          description="Same world in a new light. An OpenGL renderer, launcher and many fixes for the GOG and Steam releases of Ultima IX, our buggy friend."
          badges={[{ label: 'BETA', variant: 'secondary' }, { label: 'OPENGL', variant: 'secondary' }, { label: 'WINDOWS 64-BIT', variant: 'secondary' }]}
          actions={[{ label: 'View releases', href: `${projectUrl}/releases` }, { label: 'How to install', href: '#install', variant: 'outline' }]}
        >
          <div className="hero-emblem" aria-hidden="true"><span>IX</span></div>
          <p className="hero-footnote">Your own GOG or Steam copy of Ultima IX required. No game files included.</p>
        </Hero1>

        <div className="project-strip"><div className="site-wrap"><span>BACK TO BRITANNIA</span><span>CHOOSE YOUR SETTINGS</span><span>PLAY AS INTENDED</span></div></div>

        <section id="about" className="site-section site-wrap" aria-labelledby="about-title">
          <p className="eyebrow retro">01 / THE PROJECT</p>
          <h2 id="about-title" className="retro section-title">A new chapter<br />for a familiar world.</h2>
          <div className="feature-grid">
            {features.map(feature => (
              <PixelCard key={feature.number} font="normal" className="feature-card">
                <CardHeader font="normal"><span className="feature-number retro">{feature.number}</span><CardTitle font="normal"><h3>{feature.title}</h3></CardTitle></CardHeader>
                <CardContent font="normal"><p>{feature.description}</p><div className="feature-buttons">{feature.details.map((kind, i) => (
                  <Fragment key={kind}>
                    <FeatureDrawer kind={kind} />
                    { }
                    {feature.details.length === 3 && i === 0 && <span className="feature-buttons-break" aria-hidden="true" />}
                  </Fragment>
                ))}</div></CardContent>
              </PixelCard>
            ))}
          </div>
        </section>

        <ScreenshotComparisons />
        <DashboardGallery />

        <section id="install" className="install-section" aria-labelledby="install-title"><div className="site-section site-wrap">
          <p className="eyebrow retro">04 / GET STARTED</p>
          <h2 id="install-title" className="retro section-title">Your next visit<br />starts here.</h2>
          <p className="section-intro">Bring Ultima IX: Ascension, the GOG or Steam release, on 64-bit Windows. The standard download includes its own .NET runtime.</p>
          <ol className="install-steps">
            <li><span className="step-number retro" aria-hidden="true">01</span><h3>Get U9GL</h3><p>Choose a download on the <a href={`${projectUrl}/releases`}>releases page</a>. Use the standard zip if you are unsure which to choose.</p></li>
            <li><span className="step-number retro" aria-hidden="true">02</span><h3>Unzip beside the game</h3><p>Extract into your game folder, so <code>u9gl</code> sits beside <code>u9.exe</code>.</p><pre aria-label="Installation folder layout">{'Ultima9/\n├── u9.exe\n└── u9gl/\n    └── U9GL.exe'}</pre></li>
            <li><span className="step-number retro" aria-hidden="true">03</span><h3>Install, then play</h3><p>Open <code>u9gl\U9GL.exe</code>, choose <strong>Install</strong>, then <strong>Play</strong>. The launcher backs up anything it replaces or edits.</p></li>
          </ol>
        </div></section>

        <section className="site-section site-wrap questions" aria-labelledby="questions-title">
          <div><p className="eyebrow retro">05 / GOOD TO KNOW</p><h2 id="questions-title" className="retro section-title">Before you<br />set out.</h2></div>
          <div className="question-list">
            <details><summary>Does U9GL change my game?</summary><p>Your <code>u9.exe</code> is never edited: with Large address space on, U9GL starts a copy of it, <code>u9-laa.exe</code>. The launcher backs up anything it replaces or edits, and turning the options off gives you the original game.</p></details>
            <details><summary>Does U9GL work with fan patches?</summary><p>Yes. The launcher recognises the Forgotten World patches (1.19.01, 1.19G and 1.19H), Grandor’s Dialogue Patch (1.4 to 1.6) and Novum’s Economy and Monster patches. It tells you which are installed, when a newer version exists, and which of your saves were started before a patch that only takes effect in a new game.</p></details>
            <details><summary>Can I start a new game from the journal?</summary><p>Yes, once U9GL has made the save it needs. The game as GOG and Steam sell it ships without it, so a new game from the journal skips the opening and leaves the toolbelt, backpack and journal behind. In the launcher, open <strong>Save data</strong>, press <strong>Make the new-game snapshot</strong> and let the opening play for about two minutes; you may skip the intro video with Escape. Do this before installing fan patches.</p></details>
            <details><summary>How do I update or uninstall?</summary><p>Extract the new release over your existing <code>u9gl</code> folder, open the launcher and choose <strong>Update</strong>. Your settings are kept. To remove U9GL, choose <strong>Uninstall</strong> first, then delete the <code>u9gl</code> folder.</p></details>
            <details><summary>Is the source available?</summary><p>Yes. U9GL is source-available under its Non-Commercial Copyleft Licence. Personal, non-commercial use is free; organisational and commercial use require a paid licence. Read the <a href={`${projectUrl}/blob/main/LICENSE.md`}>licence</a> for the terms.</p></details>
            <details><summary>Where can I report a problem?</summary><p>For every bug fixed, there may be another still waiting to be found. Open an <a href={`${projectUrl}/issues`}>issue on GitHub</a> with your U9GL version, what happened, your <code>%TEMP%\u9log.txt</code> log and any crash report in <code>%TEMP%\u9gl_diag\</code>.</p></details>
          </div>
        </section>

        <section className="support-section site-section site-wrap" aria-labelledby="support-title">
          <p className="eyebrow retro">06 / SUPPORT THE PROJECT</p>
          <h2 id="support-title" className="retro section-title">Support Me</h2>
          <p className="section-intro">Enjoying U9GL? If you’d like to support my work, you can buy me a coffee or leave a tip on Ko-fi. Thank you for helping me keep building.</p>
          <div className="support-links">
            <PixelButton asChild><a href="https://buymeacoffee.com/theGreyWanderer" target="_blank" rel="noopener noreferrer">Buy Me a Coffee</a></PixelButton>
            <PixelButton asChild variant="outline"><a href="https://ko-fi.com/thegreywanderer" target="_blank" rel="noopener noreferrer">Support on Ko-fi</a></PixelButton>
          </div>
        </section>
      </main>

      <footer className="site-footer site-wrap">
        <div><a className="wordmark retro" href="#">U9<span>GL</span></a><p>A fan project by theGreyWanderer-uc.</p></div>
        <div className="footer-links"><a href={projectUrl}>GitHub</a><a href={`${projectUrl}/blob/main/CHANGELOG.md`}>Changelog</a><a href={`${projectUrl}/blob/main/LICENSING-FAQ.md`}>Licensing</a></div>
        <p className="affiliation">Not affiliated with or endorsed by Electronic Arts, Origin Systems, GOG or Valve. Ultima is a trademark of Electronic Arts.</p>
        <p className="credits">Built with <a href="https://8bitcn.com">8bitcn/ui</a> (<a href="./licenses/8bitcn-LICENSE.txt">MIT licence</a>) and Press Start 2P (<a href="./licenses/press-start-2p-OFL.txt">font licence</a>).</p>
      </footer>
    </>
  );
}
