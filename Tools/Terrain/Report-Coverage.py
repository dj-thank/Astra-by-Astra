"""Plot exported real DEM / CPU triangle evidence. Does not render game assets."""
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap, BoundaryNorm
import numpy as np

root = Path(sys.argv[1])
metrics = {'status': 'CPU_GEOMETRY_NOT_GAME', 'scenarios': {}}
source_colors = ['#596273', '#d79b47', '#91bcc7', '#b9dca1', '#468454']
source_cmap = ListedColormap(source_colors)
for scenario in ['landing', 'approach']:
    samples = {mode: np.genfromtxt(root / mode / f'{scenario}-samples.csv', delimiter=',', names=True)
               for mode in ['baseline', 'candidate']}
    candidate = samples['candidate']
    x, y = candidate['east'], candidate['north']
    scenarios = metrics['scenarios'][scenario] = {}
    for mode, a in samples.items():
        summary = json.loads((root / mode / f'{scenario}-summary.json').read_text(encoding='utf-8'))
        regions = {}
        for name, mask in {
            'surrounding_2_to_10km': (np.hypot(x, y) >= 2000) & (np.hypot(x, y) <= 10000),
            'north_massif': (np.abs(x) < 6000) & (y > 2000) & (y < 6000),
            'flight_corridor': (x > -3100) & (x < 100) & (np.abs(y) < 200),
        }.items():
            error = a['error'][mask]
            regions[name] = {'samples': int(mask.sum()), 'rmsMeters': float(np.sqrt(np.mean(error**2))),
                             'p95AbsoluteMeters': float(np.percentile(np.abs(error), 95)),
                             'maxAbsoluteMeters': float(np.max(np.abs(error)))}
        scenarios[mode] = {'geometry': summary, 'heightLoss': regions}
    fig, axes = plt.subplots(2, 3, figsize=(16, 10), constrained_layout=True)
    fig.suptitle(f'Apollo 17 / {scenario} - measured source coverage and CPU terrain geometry\n'
                 'Diagnostic projection only; not an Unreal render or photographic acceptance', fontsize=16)
    extent = [x.min()/1000, x.max()/1000, y.min()/1000, y.max()/1000]
    shape = (401, 401)
    tiers = axes[0, 0].imshow(candidate['sourceTier'].reshape(shape), origin='lower', extent=extent,
                             cmap=source_cmap, norm=BoundaryNorm(np.arange(-.5, 5.5), 5))
    axes[0, 0].set_title('Input source tier (valid-mask aware)')
    cb = fig.colorbar(tiers, ax=axes[0, 0], ticks=range(5), shrink=.75)
    cb.ax.set_yticklabels(['Global ~1.9km', '10m/global blend', '10m DTM', '5m/10m blend', '5m DTM'])
    height = axes[1, 0].imshow(candidate['height'].reshape(shape), origin='lower', extent=extent, cmap='gist_earth')
    axes[1, 0].set_title('Measured height above 1737400m datum')
    fig.colorbar(height, ax=axes[1, 0], label='meters', shrink=.75)
    for i, mode in enumerate(['baseline', 'candidate'], 1):
        data = samples[mode]
        grid = axes[0, i].imshow(np.log2(data['meshStep']/5).reshape(shape), origin='lower', extent=extent,
                                 cmap='viridis', vmin=0, vmax=7)
        axes[0, i].set_title(f'{mode.capitalize()} geometric post spacing')
        cb = fig.colorbar(grid, ax=axes[0, i], ticks=range(8), shrink=.75)
        cb.ax.set_yticklabels([f'{5*2**k}m' for k in range(8)])
        loss = axes[1, i].imshow(data['error'].reshape(shape), origin='lower', extent=extent,
                                 cmap='RdBu_r', vmin=-15, vmax=15)
        rms = scenarios[mode]['heightLoss']['surrounding_2_to_10km']['rmsMeters']
        axes[1, i].set_title(f'{mode.capitalize()} triangle height loss / 2-10km RMS {rms:.2f}m')
        fig.colorbar(loss, ax=axes[1, i], label='rendered minus measured meters', shrink=.75)
    for ax in axes.flat:
        ax.plot([-3, 0], [0, 0], color='#fcef3b', linewidth=3)
        ax.scatter([0], [0], color='#fcef3b', s=16)
        ax.set_xlabel('east from Apollo 17 landing site (km)')
        ax.set_ylabel('north (km)')
    fig.savefig(root / f'{scenario}-coverage.png', dpi=150)
    plt.close(fig)
(root / 'metrics.json').write_text(json.dumps(metrics, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(metrics, ensure_ascii=False, indent=2))
