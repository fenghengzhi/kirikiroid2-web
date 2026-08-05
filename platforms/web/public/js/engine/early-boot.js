// 早期引导：必须在首次绘制和任何 canvas.getContext 之前执行。
// 从 platforms/web/shell.html 原 <head> 内联脚本原样搬迁。
(function() {
    try {
        var params = new URLSearchParams(window.location.search);
        // Auto-load mode (?xp3= / ?game=): hide the static landing content
        // before first paint; the loading overlay takes over instead.
        if (params.get('xp3') || params.get('game')) {
            document.documentElement.classList.add('krkr2-autoload');
        }
        if (params.get('debugEarlyLogs') === '1') {
            var logs = [];
            function capture(level, args) {
                var line = '[' + level + '] ' + Array.prototype.map.call(args, function(arg) {
                    if (typeof arg === 'string') return arg;
                    try { return JSON.stringify(arg); } catch (e) { return String(arg); }
                }).join(' ');
                logs.push(line);
                if (logs.length > 5000) logs.shift();
                try {
                    sessionStorage.setItem('__krkr2EarlyLogs', JSON.stringify(logs));
                } catch (e) {}
            }
            window.__krkr2EarlyLogs = logs;
            ['log', 'warn', 'error'].forEach(function(level) {
                var original = console[level];
                console[level] = function() {
                    capture(level, arguments);
                    return original.apply(this, arguments);
                };
            });
        }

        var renderer = (params.get('renderer') || '').trim().toLowerCase();
        var preserve = renderer === 'opengl';
        if (params.has('webglPreserveDrawingBuffer')) {
            var preserveParam =
                (params.get('webglPreserveDrawingBuffer') || '').trim().toLowerCase();
            preserve = preserveParam === '1' || preserveParam === 'true';
        }

        var alpha = renderer === 'opengl' ? false : null;
        if (params.has('webglAlpha')) {
            var alphaParam = (params.get('webglAlpha') || '').trim().toLowerCase();
            alpha = !(alphaParam === '0' || alphaParam === 'false');
        }

        if (preserve || alpha !== null) {
            var originalGetContext = HTMLCanvasElement.prototype.getContext;
            HTMLCanvasElement.prototype.getContext = function(type, attrs) {
                if (this && this.id === 'canvas' &&
                    (type === 'webgl' || type === 'webgl2' ||
                     type === 'experimental-webgl')) {
                    attrs = Object.assign({}, attrs || {});
                    attrs.preserveDrawingBuffer = preserve;
                    if (alpha !== null) attrs.alpha = alpha;
                    console.warn('[webgl] context attrs override', type, attrs);
                }
                return originalGetContext.call(this, type, attrs);
            };
        }
    } catch (e) {}
})();
