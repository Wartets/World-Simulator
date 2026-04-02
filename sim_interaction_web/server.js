/**
 * Simple HTTP Server with Models API
 * 
 * Serves the static files and provides an API endpoint to list available models.
 * 
 * Usage:
 *   node sim_interaction_web/server.js
 * 
 * Or from project root:
 *   npm start
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const url = require('url');

const PORT = process.env.PORT || 3000;
const MODELS_DIR = path.join(__dirname, '..', 'models');
const WEB_DIR = __dirname;

// MIME types for static files
const MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon'
};

/**
 * Get MIME type for a file
 */
function getMimeType(filePath) {
    const ext = path.extname(filePath).toLowerCase();
    return MIME_TYPES[ext] || 'application/octet-stream';
}

/**
 * List all .simmodel directories in the models folder
 */
function listModels() {
    try {
        if (!fs.existsSync(MODELS_DIR)) {
            return [];
        }

        const entries = fs.readdirSync(MODELS_DIR, { withFileTypes: true });
        const models = [];

        for (const entry of entries) {
            if (entry.isDirectory() && entry.name.endsWith('.simmodel')) {
                const modelPath = path.join(MODELS_DIR, entry.name);
                const modelJsonPath = path.join(modelPath, 'model.json');
                const metadataPath = path.join(modelPath, 'metadata.json');

                let modelInfo = {
                    id: entry.name.replace('.simmodel', ''),
                    directory: entry.name
                };

                // Try to read model.json for the ID
                if (fs.existsSync(modelJsonPath)) {
                    try {
                        const modelData = JSON.parse(fs.readFileSync(modelJsonPath, 'utf8'));
                        modelInfo.id = modelData.id || modelInfo.id;
                        modelInfo.version = modelData.version;
                    } catch (error) {
                        console.warn(`Error reading ${modelJsonPath}:`, error.message);
                    }
                }

                // Try to read metadata.json for name and description
                if (fs.existsSync(metadataPath)) {
                    try {
                        const metadata = JSON.parse(fs.readFileSync(metadataPath, 'utf8'));
                        modelInfo.name = metadata.name || modelInfo.id;
                        modelInfo.description = metadata.description || '';
                        modelInfo.tags = metadata.tags || [];
                        modelInfo.author = metadata.author || '';
                    } catch (error) {
                        console.warn(`Error reading ${metadataPath}:`, error.message);
                    }
                }

                models.push(modelInfo);
            }
        }

        return models.sort((a, b) => a.id.localeCompare(b.id));
    } catch (error) {
        console.error('Error listing models:', error);
        return [];
    }
}

/**
 * Serve a static file
 */
function serveStaticFile(filePath, res) {
    fs.readFile(filePath, (err, data) => {
        if (err) {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('File not found');
            return;
        }

        const mimeType = getMimeType(filePath);
        res.writeHead(200, { 'Content-Type': mimeType });
        res.end(data);
    });
}

/**
 * Handle API requests
 */
function handleApiRequest(req, res) {
    const parsedUrl = url.parse(req.url, true);
    const pathname = parsedUrl.pathname;

    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    // API: List all models
    if (pathname === '/api/models' && req.method === 'GET') {
        const models = listModels();
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(models, null, 2));
        return;
    }

    // API: Get specific model info
    if (pathname.startsWith('/api/models/') && req.method === 'GET') {
        const modelId = pathname.split('/')[3];
        const models = listModels();
        const model = models.find(m => m.id === modelId);

        if (model) {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(model, null, 2));
        } else {
            res.writeHead(404, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Model not found' }));
        }
        return;
    }

    // API not found
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'API endpoint not found' }));
}

/**
 * Main request handler
 */
function handleRequest(req, res) {
    const parsedUrl = url.parse(req.url, true);
    const pathname = parsedUrl.pathname;

    // Handle API requests
    if (pathname.startsWith('/api/')) {
        handleApiRequest(req, res);
        return;
    }

    // Serve static files
    let filePath = path.join(WEB_DIR, pathname);
    
    // Default to index.html for root path
    if (pathname === '/') {
        filePath = path.join(WEB_DIR, 'index.html');
    }

    // Security: prevent directory traversal
    if (!filePath.startsWith(WEB_DIR)) {
        res.writeHead(403, { 'Content-Type': 'text/plain' });
        res.end('Forbidden');
        return;
    }

    // Check if file exists
    fs.stat(filePath, (err, stats) => {
        if (err || !stats.isFile()) {
            // Try to serve index.html for SPA routing
            if (pathname !== '/index.html') {
                serveStaticFile(path.join(WEB_DIR, 'index.html'), res);
            } else {
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('File not found');
            }
            return;
        }

        serveStaticFile(filePath, res);
    });
}

// Create and start the server
const server = http.createServer(handleRequest);

server.listen(PORT, () => {
    console.log(`=== World Simulator Interaction Server ===`);
    console.log(`Server running at http://localhost:${PORT}`);
    console.log(`API endpoint: http://localhost:${PORT}/api/models`);
    console.log(`Models directory: ${MODELS_DIR}`);
    console.log(`\nPress Ctrl+C to stop the server`);
});

// Handle graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down server...');
    server.close(() => {
        console.log('Server stopped');
        process.exit(0);
    });
});
