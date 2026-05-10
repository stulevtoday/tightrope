import i18next from 'i18next';
import { Component, type ReactNode } from 'react';

interface ErrorBoundaryProps {
  children: ReactNode;
}

interface ErrorBoundaryState {
  error: Error | null;
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = { error: null };

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { error };
  }

  private handleReset = (): void => {
    this.setState({ error: null });
  };

  render() {
    if (this.state.error) {
      return (
        <main
          role="alert"
          style={{
            minHeight: '100vh',
            display: 'grid',
            placeItems: 'center',
            padding: '2rem',
            background: 'var(--bg-elev-1)',
            color: 'var(--text-primary)',
          }}
        >
          <section style={{ maxWidth: 640, width: '100%', textAlign: 'center' }}>
            <h1 style={{ marginBottom: '0.75rem' }}>{i18next.t('error_boundary.title')}</h1>
            <p style={{ marginBottom: '1rem', color: 'var(--text-secondary)' }}>
              {i18next.t('error_boundary.message')}
            </p>
            <pre
              style={{
                textAlign: 'left',
                whiteSpace: 'pre-wrap',
                overflowWrap: 'anywhere',
                borderRadius: 8,
                padding: '0.75rem',
                border: '1px solid var(--line)',
                background: 'var(--bg-elev-2)',
                marginBottom: '1rem',
                color: 'var(--warn)',
              }}
            >
              {this.state.error.message}
            </pre>
            <button type="button" className="btn-secondary" onClick={this.handleReset}>
              {i18next.t('error_boundary.reset_ui')}
            </button>
          </section>
        </main>
      );
    }

    return this.props.children;
  }
}
