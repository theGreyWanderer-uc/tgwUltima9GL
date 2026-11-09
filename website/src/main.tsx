import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '@fontsource/press-start-2p/latin-400.css';
import './site.css';
import { U9ProjectSite } from './u9-project-site';

const siteRoot = document.getElementById('root');
if (!siteRoot) throw new Error('U9GL website root element is missing');

createRoot(siteRoot).render(
  <StrictMode>
    <U9ProjectSite />
  </StrictMode>,
);
