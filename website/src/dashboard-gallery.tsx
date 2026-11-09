const dashboardScreenshots = [
  { file: 'dashboardWorld.png', title: 'World', width: 1904, height: 1079, description: 'Your location in Britannia and the area around you.', alt: 'U9GL dashboard World page showing map coordinates and a grid of the surrounding area.' },
  { file: 'dashboardTextures.png', title: 'Textures', width: 1899, height: 1079, description: 'Texture cache usage and the status of texture overrides.', alt: 'U9GL dashboard Textures page showing a cache usage chart, texture counts and override statistics.' },
  { file: 'dashboardEvents.png', title: 'Events', width: 1897, height: 1079, description: 'A view of the game log, with filters for events and messages.', alt: 'U9GL dashboard Events page showing log filters and a scrolling list of game messages.' },
];

export function DashboardGallery() {
  return (
    <section id="dashboard" className="dashboard-section site-section site-wrap" aria-labelledby="dashboard-title">
      <p className="eyebrow retro">03 / THE WEB DASHBOARD</p>
      <h2 id="dashboard-title" className="retro section-title">A window into the game.</h2>
      <p className="section-intro">The optional web dashboard brings the world, textures and game events into your browser. Open any screenshot to view it at full size.</p>
      <div className="dashboard-gallery">
        {dashboardScreenshots.map(screenshot => (
          <figure className="dashboard-screenshot" key={screenshot.file}>
            <a href={`${import.meta.env.BASE_URL}screenshots/${screenshot.file}`} target="_blank" rel="noopener" aria-label={`Open ${screenshot.title} dashboard screenshot at full size`}>
              <img src={`${import.meta.env.BASE_URL}screenshots/${screenshot.file}`} width={screenshot.width} height={screenshot.height} loading="lazy" decoding="async" alt={screenshot.alt} />
              <span className="dashboard-image-hint">View full size</span>
            </a>
            <figcaption><h3>{screenshot.title}</h3><p>{screenshot.description}</p></figcaption>
          </figure>
        ))}
      </div>
    </section>
  );
}
