import type { RoutingMode, ScoringModel } from '../../../shared/types';
import { sliderTrackStyle } from '../../../shared/styles';

interface RoutingStrategySectionProps {
  routingModes: RoutingMode[];
  routingMode: string;
  scoringModel: ScoringModel;
  onSetRoutingMode: (modeId: string) => void;
  onSetStrategyParam: (modeId: string, key: string, value: number) => void;
  onSetScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
  onSetHeadroomWeight: (key: 'wp' | 'ws', value: number) => void;
}

const WEIGHT_LABELS: Record<'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', string> = {
  alpha: 'α queue',
  beta: 'β latency',
  gamma: 'γ error',
  delta: 'δ headroom',
  zeta: 'ζ cost',
  eta: 'η cooldown',
};

export function RoutingStrategySection({
  routingModes,
  routingMode,
  scoringModel,
  onSetRoutingMode,
  onSetStrategyParam,
  onSetScoringWeight,
  onSetHeadroomWeight,
}: RoutingStrategySectionProps) {
  const mode = routingModes.find((candidate) => candidate.id === routingMode) ?? routingModes[0];

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Routing strategy</h3>
        <p>Load balancing algorithm and composite scoring weights</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Strategy</strong>
          <span>How requests are distributed across eligible accounts</span>
        </div>
        <select className="setting-select" value={routingMode} onChange={(event) => onSetRoutingMode(event.target.value)}>
          {routingModes.map((candidate) => (
            <option key={candidate.id} value={candidate.id}>
              {candidate.label}
            </option>
          ))}
        </select>
      </div>

      <div style={{ padding: '0.45rem 0.6rem', borderBottom: '1px solid var(--border)' }}>
        <span className="strategy-desc">{mode.desc}</span>
        <div className="strategy-formula">{mode.formula}</div>
        {mode.params &&
          Object.entries(mode.params).map(([key, value]) => (
            <div key={key} className="strategy-param-row">
              <label>{key === 'rho' ? 'ρ (exponent)' : key}</label>
              <input
                type="number"
                min={0.1}
                max={10}
                step={0.1}
                value={value}
                onChange={(event) => onSetStrategyParam(mode.id, key, Number(event.target.value))}
              />
            </div>
          ))}
      </div>

      {mode.usesComposite && (
        <div className="composite-weights">
          <h4>Composite score weights</h4>
          <div className="composite-formula">S(a,r) = α·q + β·l + γ·e + δ·(1−h) + ζ·cost + η·c</div>
          <div className="weight-grid" style={{ marginTop: '0.35rem' }}>
            {(Object.keys(WEIGHT_LABELS) as Array<keyof typeof WEIGHT_LABELS>).map((key) => (
              <div key={key} className="weight-item">
                <label>{WEIGHT_LABELS[key]}</label>
                <input
                  type="range"
                  min={0}
                  max={1}
                  step={0.01}
                  value={scoringModel[key]}
                  style={sliderTrackStyle(scoringModel[key])}
                  onChange={(event) => onSetScoringWeight(key, Number(event.target.value))}
                />
                <strong>{scoringModel[key].toFixed(2)}</strong>
              </div>
            ))}
          </div>
          <div className="headroom-weights">
            <label>w_p</label>
            <input
              type="number"
              min={0}
              max={1}
              step={0.05}
              value={scoringModel.wp}
              onChange={(event) => onSetHeadroomWeight('wp', Number(event.target.value))}
            />
            <label>w_s</label>
            <input
              type="number"
              min={0}
              max={1}
              step={0.05}
              value={scoringModel.ws}
              onChange={(event) => onSetHeadroomWeight('ws', Number(event.target.value))}
            />
            <span style={{ fontSize: '10.5px', color: 'var(--text-tertiary)' }}>h(a) = 1 − (w_p·u_p + w_s·u_s)</span>
          </div>
        </div>
      )}
    </div>
  );
}
