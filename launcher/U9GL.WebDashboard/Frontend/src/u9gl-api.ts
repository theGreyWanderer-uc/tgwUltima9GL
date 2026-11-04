export function apiUrl(path: string, query: Record<string, string> = {}): string {
  const token = new URLSearchParams(window.location.search).get("t");
  const params = new URLSearchParams(query);
  if (token) params.set("t", token);
  const q = params.toString();
  return q ? `${path}?${q}` : path;
}
