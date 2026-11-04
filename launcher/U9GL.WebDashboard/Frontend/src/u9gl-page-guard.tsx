import { Component, type ReactNode } from "react";

export class U9glPageGuard extends Component<{ page: string; children: ReactNode }, { error: string | null; page: string }> {
  state = { error: null as string | null, page: this.props.page };
  static getDerivedStateFromError(error: unknown) { return { error: error instanceof Error ? error.message : String(error) }; }
  static getDerivedStateFromProps(props: { page: string }, state: { error: string | null; page: string }) {
    return props.page !== state.page ? { error: null, page: props.page } : null;
  }
  render() {
    if (this.state.error === null) return this.props.children;
    return (
      <div role="alert" className="u9gl-page-error">
        <p className="lcars-copy">This page could not be drawn: {this.state.error}</p>
        <p className="lcars-supporting">The other pages still work. Reloading the page may help; if it keeps happening, the Events page and the launcher log can say more.</p>
      </div>
    );
  }
}
