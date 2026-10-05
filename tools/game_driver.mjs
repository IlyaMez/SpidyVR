// Run from the Computer Use node_repl session, after initializing @oai/sky.
// This wraps the supported sky API; it does not implement a helper protocol.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const gameExe = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Marvel's Spider-Man Remastered\\Spider-Man.exe";

export class GameDriver {
    constructor(sky) {
        this.sky = sky;
        this.window = null;
        this.observation = null;
        this.sequence = 0;
        this.output = path.join(projectRoot, 'reports', 'automation', new Date().toISOString().replace(/[:.]/g, '-'));
    }

    async discover() {
        const windows = await this.sky.list_windows();
        // Always select returned objects; never fabricate a window handle.
        return windows.filter(w => w.app.toLowerCase() === `process:${gameExe}`.toLowerCase());
    }

    async select() {
        this.observation = null;
        const candidates = await this.discover();
        if (candidates.length !== 1) {
            this.window = null;
            throw new Error(`Expected one Spider-Man window, found ${candidates.length}: ${JSON.stringify(candidates)}`);
        }
        this.window = await this.sky.get_window({ id: candidates[0].id, app: candidates[0].app });
        return this.window;
    }

    async launch() {
        this.observation = null;
        await this.sky.launch_app({ app: gameExe });
        return this.select();
    }

    async observe({ text = false, label } = {}) {
        if (!this.window) await this.select();
        this.observation = null;
        const state = await this.sky.get_window_state({
            window: this.window, include_screenshot: true, include_text: text,
        });
        this.window = state.window;
        this.observation = state;
        if (label) await this.record(label);
        // sky displays the image. Keep base64 and unrelated app data out of logs.
        return this.summary();
    }

    summary() {
        const state = this.observation;
        return {
            window: this.window,
            screenshots: state?.screenshots.map(s => ({
                id: s.id, width: s.width, height: s.height,
            })) ?? [],
            accessibility: state?.accessibility ?? null,
        };
    }

    async activate() {
        if (!this.window) await this.select();
        this.observation = null;
        await this.sky.activate_window({ window: this.window });
        return this.observe();
    }

    async act(action, { text = false, label } = {}) {
        const observed = this.observation;
        if (!observed) throw new Error('Observe the target and inspect its screenshot before input.');
        const screenshotId = observed.screenshots?.[0]?.id;
        if (['click', 'drag'].includes(action.kind) && screenshotId == null)
            throw new Error('Coordinate input needs the latest screenshot.');
        this.observation = null;
        try {
            if (action.kind === 'key') {
                await this.sky.press_key({ window: observed.window, key: action.key });
            } else if (action.kind === 'click') {
                await this.sky.click({ window: observed.window, screenshotId,
                    x: action.x, y: action.y, mouse_button: action.button ?? 'left' });
            } else if (action.kind === 'drag') {
                await this.sky.drag({ window: observed.window, screenshotId,
                    from_x: action.fromX, from_y: action.fromY, to_x: action.toX, to_y: action.toY });
            } else {
                throw new Error(`Unsupported action: ${action.kind}`);
            }
            await this.log({ kind: 'input', action, window: observed.window });
            return await this.observe({ text, label });
        } catch (cause) {
            this.observation = null;
            await this.log({kind: 'input_or_refresh_error', action, message: String(cause?.message ?? cause)});
            throw new Error(`Input or refresh outcome is uncertain: ${cause?.message ?? cause}. Observe again before another input.`, { cause });
        }
    }

    async log(event) {
        await fs.mkdir(this.output, { recursive: true });
        await fs.appendFile(path.join(this.output, 'events.jsonl'), JSON.stringify({
            time: new Date().toISOString(), ...event,
        }) + '\n');
    }

    async record(label) {
        if (!this.observation) throw new Error('No fresh observation to record.');
        if (!/^[a-z0-9_-]{1,64}$/i.test(label)) throw new Error('Use a short file-safe evidence label.');
        await fs.mkdir(this.output, { recursive: true });
        const files = [];
        for (const [index, screenshot] of this.observation.screenshots.entries()) {
            const match = /^data:image\/(png|jpeg);base64,([A-Za-z0-9+/=\r\n]+)$/.exec(screenshot.url);
            if (!match) throw new Error('Unexpected screenshot encoding.');
            const filename = `${String(++this.sequence).padStart(4, '0')}-${label}-${index}.${match[1] === 'jpeg' ? 'jpg' : 'png'}`;
            const destination = path.join(this.output, filename);
            // User requested saved images for autonomous game testing.
            await fs.writeFile(destination, Buffer.from(match[2], 'base64'), { flag: 'wx' });
            files.push(destination);
        }
        await this.log({ kind: 'capture', label, window: this.observation.window, files });
        return files;
    }
}
