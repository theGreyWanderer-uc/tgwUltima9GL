import { createRoot } from "react-dom/client";
import { U9glDiagnosticsView } from "./u9gl-diagnostics-view";
import "./u9gl-dashboard.css";

const root = document.getElementById("root");
if (!root) throw new Error("U9GL dashboard: root element is missing.");

createRoot(root).render(<U9glDiagnosticsView />);
