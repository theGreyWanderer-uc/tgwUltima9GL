import { useEffect, useRef, useState } from "react";
import { LcarsPanel } from "./components/ui/lcars/lcars-geometry";
import { filterLogEvents, parseLogPage, type DashboardLogEvent, type DashboardLogSummary } from "./u9gl-log";
import { apiUrl } from "./u9gl-api";

const kept = 3000;
const withoutTag = (e: DashboardLogEvent) => e.tag !== "-" && e.text.startsWith(e.tag + " ") ? e.text.slice(e.tag.length).trimStart() : e.text;

export function U9glEvents() {
  const [events, setEvents] = useState<DashboardLogEvent[]>([]);
  const [summary, setSummary] = useState<DashboardLogSummary | null>(null);
  const [dropped, setDropped] = useState(0);
  const [error, setError] = useState("");
  const [query, setQuery] = useState("");
  const [level, setLevel] = useState<"all" | "warning" | "fault">("all");
  const [tag, setTag] = useState("");
  const [follow, setFollow] = useState(true);
  const cursor = useRef(0);
  const list = useRef<HTMLOListElement>(null);

  useEffect(() => {
    let stopped = false;
    const fetchPage = async () => {
      try {
        const response = await fetch(apiUrl("/api/v1/log", { after: String(cursor.current) }), { cache: "no-store" });
        const page = parseLogPage(await response.text());
        if (stopped) return;
        if (page.oldest > cursor.current + 1 && cursor.current > 0) setDropped(d => d + page.oldest - cursor.current - 1);
        cursor.current = Math.max(cursor.current, page.next);
        setSummary(page.summary);
        if (page.events.length) setEvents(old => [...old, ...page.events].slice(-kept));
        setError("");
        if (page.events.length === 500) { void fetchPage(); return; }
      } catch (e) { if (!stopped) setError(e instanceof Error ? e.message : "Could not read the game log."); }
    };
    void fetchPage();
    const timer = window.setInterval(() => void fetchPage(), 1000);
    return () => { stopped = true; window.clearInterval(timer); };
  }, []);

  const shown = filterLogEvents(events, query, level, tag).slice(-1000);
  const tags = [...new Set(events.map(e => e.tag))].sort();
  useEffect(() => { if (follow && list.current) list.current.scrollTop = list.current.scrollHeight; }, [shown.length, follow]);

  return (
    <>
      <LcarsPanel id="events-source" title="Game log" headingLevel="h3" className="u9gl-readouts">
        <p className="lcars-copy">
          {summary?.runPid ? `Run PID ${summary.runPid}${summary.runVersion ? ` · ${summary.runVersion}` : ""} · ` : ""}
          {summary ? `${summary.faults} faults · ${summary.warnings} warnings this run` : "Reading the game log..."}
        </p>
        <p className="lcars-supporting">
          The game buffers its render log and writes it in 64 KB batches and at exit, so lines arrive in bursts;
          the crash report is written as it happens, so faults appear within a second.
          {summary?.logGrewAgoMs != null ? ` The log last grew ${(summary.logGrewAgoMs / 1000).toFixed(0)} s ago.` : ""}
          {dropped ? ` ${dropped} older events were dropped before this page saw them.` : ""}
        </p>
        {error && <p className="lcars-supporting" role="alert">{error}</p>}
      </LcarsPanel>
      <section className="u9gl-controls u9gl-event-controls" aria-label="Event filters">
        <label>Filter<input type="search" value={query} onChange={e => setQuery(e.target.value)} placeholder="text or address" /></label>
        <label>Level<select value={level} onChange={e => setLevel(e.target.value === "fault" ? "fault" : e.target.value === "warning" ? "warning" : "all")}>
          <option value="all">All</option><option value="warning">Warnings and faults</option><option value="fault">Faults</option>
        </select></label>
        <label>Tag<select value={tag} onChange={e => setTag(e.target.value)}>
          <option value="">All tags</option>{tags.map(t => <option key={t} value={t}>{t}</option>)}
        </select></label>
        <label className="u9gl-freeze-control"><input type="checkbox" checked={follow} onChange={e => setFollow(e.target.checked)} />Follow new events</label>
      </section>
      <p className="lcars-supporting">{shown.length} of {events.length} events shown{shown.length === 1000 ? " (the latest 1,000)" : ""}.</p>
      <ol className="u9gl-event-list" ref={list} aria-label="Game log events" tabIndex={0}>
        {shown.map(e => (
          <li key={e.seq} data-level={e.level} data-source={e.source}>
            <span className="u9gl-event-tag">{e.tag}</span>
            {e.detail
              ? <details><summary>{withoutTag(e)}</summary><pre>{e.detail}</pre></details>
              : <span className="u9gl-event-text">{withoutTag(e)}</span>}
          </li>
        ))}
      </ol>
    </>
  );
}
