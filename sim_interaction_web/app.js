/**
 * World Simulator Interaction Graph
 * 
 * Interactive force-directed graph visualization using vis.js Network
 * Displays subsystem and state-field dependencies from .simmodel files
 */

const appState = {
    data: null,
    network: null,
    nodes: null,
    edges: null,
    selection: null,
    zoomTimeout: null,
    panTimeout: null,
};

// DOM element references - will be populated on DOM ready
let graphContainer = null;
let countsLabel = null;
let selectionTitle = null;
let selectionSubtitle = null;
let detailContent = null;
let readCount = null;
let writeCount = null;

function initializeDOMReferences() {
    graphContainer = document.getElementById("graph");
    countsLabel = document.getElementById("countsLabel");
    selectionTitle = document.getElementById("selectionTitle");
    selectionSubtitle = document.getElementById("selectionSubtitle");
    detailContent = document.getElementById("detailContent");
    readCount = document.getElementById("readCount");
    writeCount = document.getElementById("writeCount");
    
    console.log("DOM references initialized:", {
        graphContainer: !!graphContainer,
        countsLabel: !!countsLabel,
        selectionTitle: !!selectionTitle,
        detailContent: !!detailContent
    });
}

function escapeHtml(value) {
    return String(value ?? "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#39;");
}

function formatDisplayLabel(value) {
    return String(value ?? "").replaceAll("_", " ");
}

function getNodeById(id) {
    return appState.data?.nodes.find((node) => node.id === id) ?? null;
}

function createSummaryCard(label, value) {
    return `
        <div class="summary-card">
            <span class="label">${escapeHtml(label)}</span>
            <span class="value">${escapeHtml(value)}</span>
        </div>
    `;
}

function createMetaPill(label, value) {
    return `<span class="meta-pill">${escapeHtml(label)} <strong>${escapeHtml(value)}</strong></span>`;
}

function createDetailItem(label, value, options = {}) {
    const className = options.code ? "detail-item code" : "detail-item";
    return `
        <div class="${className}">
            <span class="label">${escapeHtml(label)}</span>
            <span class="value">${escapeHtml(value)}</span>
        </div>
    `;
}

function createInteractionCard(interaction) {
    const source = getNodeById(interaction.source);
    const target = getNodeById(interaction.target);
    const sourceLabel = formatDisplayLabel(source?.label ?? interaction.source);
    const targetLabel = formatDisplayLabel(target?.label ?? interaction.target);

    return `
        <button class="relation-card ${escapeHtml(interaction.mode)}" type="button" data-interaction-id="${escapeHtml(interaction.id)}">
            <div class="relation-top">
                <span class="relation-title">${escapeHtml(interaction.summary)}</span>
                <span class="relation-tag">${escapeHtml(interaction.mode)}</span>
            </div>
            <div class="relation-meta">
                ${escapeHtml(sourceLabel)} -> ${escapeHtml(targetLabel)}
            </div>
        </button>
    `;
}

function bindDetailActions() {
    detailContent.querySelectorAll("[data-interaction-id]").forEach((button) => {
        button.addEventListener("click", () => {
            selectInteractionById(button.dataset.interactionId);
        });
    });

    detailContent.querySelectorAll("[data-node-id]").forEach((button) => {
        button.addEventListener("click", () => {
            selectNodeById(button.dataset.nodeId);
        });
    });
}

function renderOverview() {
    const interactions = appState.data?.interactions ?? [];
    const nodes = appState.data?.nodes ?? [];
    const reads = interactions.filter((interaction) => interaction.mode === "read").length;
    const writes = interactions.filter((interaction) => interaction.mode === "write").length;

    selectionTitle.textContent = "Overview";
    selectionSubtitle.textContent = "Choose a node or connection to inspect its details.";

    detailContent.innerHTML = `
        <div class="summary-grid">
            ${createSummaryCard("Total nodes", nodes.length)}
            ${createSummaryCard("Total interactions", interactions.length)}
            ${createSummaryCard("Read edges", reads)}
            ${createSummaryCard("Write edges", writes)}
        </div>

        <section class="detail-section">
            <h3>How to use this view</h3>
            <p>
                Click any node in the graph to see the interactions connected to it. 
                Click any connection to read the full interaction details.
                Drag nodes to reposition them. The graph uses force-directed layout.
            </p>
        </section>
    `;
}

function renderNodeDetails(nodeData) {
    const node = getNodeById(nodeData.id);
    if (!node) {
        renderOverview();
        return;
    }

    const interactions = appState.data?.interactions ?? [];
    const incoming = interactions
        .filter((interaction) => interaction.target === node.id)
        .sort((left, right) => left.summary.localeCompare(right.summary));
    const outgoing = interactions
        .filter((interaction) => interaction.source === node.id)
        .sort((left, right) => left.summary.localeCompare(right.summary));

    const typeLabel = node.group === "subsystem" ? "Subsystem" : "State field";
    const incomingTitle = node.group === "subsystem" ? "Incoming dependencies" : "Written by";
    const outgoingTitle = node.group === "subsystem" ? "Outgoing effects" : "Used by";

    selectionTitle.textContent = formatDisplayLabel(node.label);
    selectionSubtitle.textContent = `${typeLabel}.`;

    detailContent.innerHTML = `
        <div class="meta-pills">
            ${createMetaPill("Type", typeLabel)}
            ${createMetaPill("Incoming", incoming.length)}
            ${createMetaPill("Outgoing", outgoing.length)}
        </div>

        <div class="summary-grid">
            ${createSummaryCard("Linked interactions", incoming.length + outgoing.length)}
        </div>

        <section class="detail-section">
            <h3>${escapeHtml(incomingTitle)}</h3>
            ${
                incoming.length
                    ? `<div class="relation-list">${incoming.map((interaction) => createInteractionCard(interaction)).join("")}</div>`
                    : `<div class="empty-state">No incoming interactions.</div>`
            }
        </section>

        <section class="detail-section">
            <h3>${escapeHtml(outgoingTitle)}</h3>
            ${
                outgoing.length
                    ? `<div class="relation-list">${outgoing.map((interaction) => createInteractionCard(interaction)).join("")}</div>`
                    : `<div class="empty-state">No outgoing interactions.</div>`
            }
        </section>
    `;

    bindDetailActions();
}

function renderInteractionDetails(interaction) {
    const source = getNodeById(interaction.source);
    const target = getNodeById(interaction.target);
    const sourceLabel = formatDisplayLabel(source?.label ?? interaction.source);
    const targetLabel = formatDisplayLabel(target?.label ?? interaction.target);

    selectionTitle.textContent = interaction.summary;
    selectionSubtitle.textContent = `${interaction.mode === "read" ? "Read" : "Write"} interaction.`;

    detailContent.innerHTML = `
        <div class="meta-pills">
            ${createMetaPill("Mode", interaction.mode)}
            ${createMetaPill("Interaction", interaction.id)}
        </div>

        <div class="detail-list">
            ${createDetailItem("Source", sourceLabel)}
            ${createDetailItem("Target", targetLabel)}
            ${createDetailItem("Detail", interaction.detail)}
            ${createDetailItem("Equation", interaction.equation, { code: true })}
            ${createDetailItem("Evidence", interaction.evidence, { code: true })}
        </div>

        <div class="action-row">
            <button class="secondary-button" type="button" data-node-id="${escapeHtml(interaction.source)}">Inspect source</button>
            <button class="secondary-button" type="button" data-node-id="${escapeHtml(interaction.target)}">Inspect target</button>
        </div>
    `;

    bindDetailActions();
}

function updateStats() {
    const interactions = appState.data?.interactions ?? [];
    const nodes = appState.data?.nodes ?? [];
    const reads = interactions.filter((interaction) => interaction.mode === "read").length;
    const writes = interactions.filter((interaction) => interaction.mode === "write").length;

    readCount.textContent = String(reads);
    writeCount.textContent = String(writes);
    countsLabel.textContent = `${nodes.length} nodes / ${interactions.length} interactions`;
}

function makeVisData() {
    const interactions = appState.data?.interactions ?? [];
    const nodes = appState.data?.nodes ?? [];

    // Create nodes array for vis.js
    const visNodes = nodes.map((node) => ({
        id: node.id,
        label: formatDisplayLabel(node.label),
        group: node.group,
        title: node.description || node.label,
        color: {
            background: node.group === "subsystem" ? "#243148" : "#1b2230",
            border: node.group === "subsystem" ? "#5870a3" : "#3c4868",
            highlight: {
                background: node.group === "subsystem" ? "#2d3f5a" : "#243148",
                border: "#4c73b0",
            },
        },
        font: {
            color: "#f2f2ff",
            size: 14,
            face: "Trebuchet MS, Segoe UI, sans-serif",
        },
        shape: "box",
        margin: 10,
        borderWidth: 2,
        borderWidthSelected: 3,
    }));

    // Create edges array for vis.js
    const visEdges = interactions.map((interaction) => ({
        id: interaction.id,
        from: interaction.source,
        to: interaction.target,
        label: interaction.mode,
        title: interaction.summary,
        color: {
            color: interaction.mode === "read" ? "#66a8f0" : "#82c0ff",
            highlight: "#7fb0ff",
            hover: "#7fb0ff",
        },
        arrows: {
            to: {
                enabled: true,
                scaleFactor: 0.8,
            },
        },
        font: {
            color: "#9aa6c3",
            size: 10,
            strokeWidth: 3,
            strokeColor: "#10141d",
        },
        width: 2,
        smooth: {
            type: "continuous",
            roundness: 0.5,
        },
    }));

    return { visNodes, visEdges };
}

function ensureNetwork() {
    console.log("ensureNetwork called");

    const { visNodes, visEdges } = makeVisData();
    console.log("Created vis data:", visNodes.length, "nodes,", visEdges.length, "edges");

    appState.nodes = new vis.DataSet(visNodes);
    appState.edges = new vis.DataSet(visEdges);

    const options = {
        nodes: {
            shape: "box",
            margin: 10,
            font: {
                color: "#f2f2ff",
                size: 14,
                face: "Trebuchet MS, Segoe UI, sans-serif",
            },
            borderWidth: 2,
            shadow: {
                enabled: true,
                color: "rgba(0,0,0,0.3)",
                size: 10,
                x: 3,
                y: 3,
            },
        },
        edges: {
            width: 2,
            smooth: {
                type: "continuous",
                roundness: 0.5,
            },
            arrows: {
                to: {
                    enabled: true,
                    scaleFactor: 0.8,
                },
            },
            font: {
                color: "#9aa6c3",
                size: 10,
                strokeWidth: 3,
                strokeColor: "#10141d",
            },
            shadow: {
                enabled: true,
                color: "rgba(0,0,0,0.2)",
                size: 5,
                x: 2,
                y: 2,
            },
        },
        physics: {
            enabled: false,
            forceAtlas2Based: {
                gravitationalConstant: -30,
                centralGravity: 0.01,
                springLength: 150,
                springConstant: 0.08,
                damping: 0.4,
                avoidOverlap: 0.5,
            },
            solver: "forceAtlas2Based",
            stabilization: {
                enabled: true,
                iterations: 1000,
                updateInterval: 25,
                onlyDynamicEdges: false,
                fit: true,
            },
            timestep: 0.5,
            adaptiveTimestep: true,
        },
        interaction: {
            hover: true,
            tooltipDelay: 200,
            hideEdgesOnDrag: false,
            hideNodesOnDrag: false,
            navigationButtons: false,
            keyboard: {
                enabled: true,
                bindToWindow: false,
            },
        },
        layout: {
            randomSeed: 42,
            improvedLayout: true,
        },
    };

    appState.network = new vis.Network(
        graphContainer,
        {
            nodes: appState.nodes,
            edges: appState.edges,
        },
        options
    );

    // Enable physics after network is created
    appState.network.setOptions({ physics: { enabled: true } });

    // Event handlers
    appState.network.on("click", (params) => {
        if (params.nodes.length > 0) {
            const nodeId = params.nodes[0];
            const node = appState.nodes.get(nodeId);
            appState.selection = { type: "node", id: nodeId };
            renderNodeDetails(node);
        } else if (params.edges.length > 0) {
            const edgeId = params.edges[0];
            const edge = appState.edges.get(edgeId);
            appState.selection = { type: "edge", id: edgeId };
            renderInteractionDetails(edge);
        } else {
            appState.selection = null;
            renderOverview();
        }
    });

    appState.network.on("dragStart", () => {
        appState.network.setOptions({ physics: { enabled: false } });
    });

    appState.network.on("dragEnd", () => {
        setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 100);
    });

    appState.network.on("zoom", () => {
        appState.network.setOptions({ physics: { enabled: false } });
        clearTimeout(appState.zoomTimeout);
        appState.zoomTimeout = setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 300);
    });

    appState.network.on("pan", () => {
        appState.network.setOptions({ physics: { enabled: false } });
        clearTimeout(appState.panTimeout);
        appState.panTimeout = setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 300);
    });
}

function selectNodeById(id) {
    if (!appState.network) return;

    appState.network.selectNodes([id]);
    const node = appState.nodes.get(id);
    if (node) {
        appState.selection = { type: "node", id };
        renderNodeDetails(node);
    }
}

function selectInteractionById(id) {
    if (!appState.network) return;

    appState.network.selectEdges([id]);
    const edge = appState.edges.get(id);
    if (edge) {
        appState.selection = { type: "edge", id };
        renderInteractionDetails(edge);
    }
}

function applyGraph() {
    console.log("applyGraph called, data:", appState.data ? "present" : "null");
    
    if (!appState.data) {
        console.warn("applyGraph called but no data available");
        return;
    }

    // Verify DOM elements exist
    if (!graphContainer) {
        console.error("Graph container not found in DOM");
        return;
    }
    
    console.log("Graph container found, dimensions:", graphContainer.clientWidth, "x", graphContainer.clientHeight);

    // Ensure container has dimensions
    if (graphContainer.clientWidth === 0 || graphContainer.clientHeight === 0) {
        console.warn("Graph container has no dimensions, forcing redraw...");
        graphContainer.style.width = "100%";
        graphContainer.style.height = "100%";
        // Force browser to calculate layout
        graphContainer.offsetHeight;
    }

    // Create fresh data for vis.js to ensure no stale references
    const { visNodes, visEdges } = makeVisData();
    console.log("Created vis data with", visNodes.length, "nodes and", visEdges.length, "edges");

    // Destroy old network completely
    if (appState.network) {
        console.log("Destroying existing network");
        try {
            appState.network.destroy();
        } catch (e) {
            console.warn("Error destroying network:", e);
        }
        appState.network = null;
    }
    
    // Clear DataSets completely
    appState.nodes = null;
    appState.edges = null;
    
    // Create new DataSets
    appState.nodes = new vis.DataSet(visNodes);
    appState.edges = new vis.DataSet(visEdges);
    
    const options = {
        nodes: {
            shape: "box",
            margin: 10,
            font: {
                color: "#f2f2ff",
                size: 14,
                face: "Trebuchet MS, Segoe UI, sans-serif",
            },
            borderWidth: 2,
            shadow: {
                enabled: true,
                color: "rgba(0,0,0,0.3)",
                size: 10,
                x: 3,
                y: 3,
            },
        },
        edges: {
            width: 2,
            smooth: {
                type: "continuous",
                roundness: 0.5,
            },
            arrows: {
                to: {
                    enabled: true,
                    scaleFactor: 0.8,
                },
            },
            font: {
                color: "#9aa6c3",
                size: 10,
                strokeWidth: 3,
                strokeColor: "#10141d",
            },
            shadow: {
                enabled: true,
                color: "rgba(0,0,0,0.2)",
                size: 5,
                x: 2,
                y: 2,
            },
        },
        physics: {
            enabled: false,
            forceAtlas2Based: {
                gravitationalConstant: -30,
                centralGravity: 0.01,
                springLength: 150,
                springConstant: 0.08,
                damping: 0.4,
                avoidOverlap: 0.5,
            },
            solver: "forceAtlas2Based",
            stabilization: {
                enabled: true,
                iterations: 1000,
                updateInterval: 25,
                onlyDynamicEdges: false,
                fit: true,
            },
            timestep: 0.5,
            adaptiveTimestep: true,
        },
        interaction: {
            hover: true,
            tooltipDelay: 200,
            hideEdgesOnDrag: false,
            hideNodesOnDrag: false,
            navigationButtons: false,
            keyboard: {
                enabled: true,
                bindToWindow: false,
            },
        },
        layout: {
            randomSeed: 42,
            improvedLayout: true,
        },
    };

    console.log("Creating new network...");
    appState.network = new vis.Network(
        graphContainer,
        {
            nodes: appState.nodes,
            edges: appState.edges,
        },
        options
    );

    // Enable physics after network is created
    appState.network.setOptions({ physics: { enabled: true } });

    // Event handlers
    appState.network.on("click", (params) => {
        if (params.nodes.length > 0) {
            const nodeId = params.nodes[0];
            const node = appState.nodes.get(nodeId);
            appState.selection = { type: "node", id: nodeId };
            renderNodeDetails(node);
        } else if (params.edges.length > 0) {
            const edgeId = params.edges[0];
            const edge = appState.edges.get(edgeId);
            appState.selection = { type: "edge", id: edgeId };
            renderInteractionDetails(edge);
        } else {
            appState.selection = null;
            renderOverview();
        }
    });

    appState.network.on("dragStart", () => {
        appState.network.setOptions({ physics: { enabled: false } });
    });

    appState.network.on("dragEnd", () => {
        setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 100);
    });

    appState.network.on("zoom", () => {
        appState.network.setOptions({ physics: { enabled: false } });
        clearTimeout(appState.zoomTimeout);
        appState.zoomTimeout = setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 300);
    });

    appState.network.on("pan", () => {
        appState.network.setOptions({ physics: { enabled: false } });
        clearTimeout(appState.panTimeout);
        appState.panTimeout = setTimeout(() => {
            appState.network.setOptions({ physics: { enabled: true } });
        }, 300);
    });
    
    updateStats();
    renderOverview();
    
    console.log("Graph applied successfully");
}

async function loadInteractionData(modelId) {
    if (!modelId) {
        throw new Error("No model selected");
    }

    console.log("loadInteractionData called with:", modelId);
    
    // Clear cache for fresh load to ensure we get latest data
    SimModelLoader.clearCache();
    
    const data = await SimModelLoader.loadModel(modelId);
    console.log("Data loaded, nodes:", data?.nodes?.length, "interactions:", data?.interactions?.length);
    appState.data = data;
}

async function populateModelSelector() {
    const selector = document.getElementById("modelSelector");
    if (!selector) return;

    try {
        const models = await SimModelLoader.getAvailableModels();

        selector.innerHTML = "";

        if (models.length === 0) {
            selector.innerHTML = '<option value="">No models found</option>';
            return;
        }

        models.forEach((model) => {
            const option = document.createElement("option");
            option.value = model.id;
            option.textContent = model.name || model.id;
            selector.appendChild(option);
        });

        selector.value = models[0].id;
    } catch (error) {
        console.error("Failed to populate model selector:", error);
        selector.innerHTML = '<option value="">Error loading models</option>';
    }
}

async function initialize() {
    // Initialize DOM references first
    initializeDOMReferences();
    
    try {
        await populateModelSelector();

        const selector = document.getElementById("modelSelector");
        
        // Wait for DOM to be fully ready before initializing
        if (!graphContainer) {
            console.error("Graph container not found, retrying...");
            setTimeout(initialize, 100);
            return;
        }

        const modelId = selector?.value;
        console.log("Initial modelId from selector:", modelId);

        if (modelId) {
            await loadInteractionData(modelId);
            applyGraph();
        }

        if (selector) {
            selector.addEventListener("change", async (event) => {
                // Use event.target.value explicitly to avoid any DOM timing issues
                const newModelId = event.target.value;
                console.log("Model changed event fired, new model:", newModelId);
                
                if (!newModelId) {
                    console.warn("No model ID in change event");
                    return;
                }
                
                try {
                    console.log("Loading interaction data for:", newModelId);
                    await loadInteractionData(newModelId);
                    console.log("Data loaded, applying graph...");
                    
                    // Get the value directly from selector to ensure consistency
                    console.log("Current selector value:", selector.value);
                    
                    applyGraph();
                    console.log("Graph applied successfully");
                } catch (error) {
                    console.error("Error loading model:", error);
                    countsLabel.textContent = `Error: ${error.message}`;
                    selectionTitle.textContent = "Unable to load the graph";
                    selectionSubtitle.textContent = "The interaction data could not be read.";
                    detailContent.innerHTML = `
                        <div class="empty-state">${escapeHtml(error.message)}</div>
                    `;
                }
            });
        }
    } catch (error) {
        console.error("Initialization error:", error);
        countsLabel.textContent = `Error: ${error.message}`;
        selectionTitle.textContent = "Unable to load the graph";
        selectionSubtitle.textContent = "The interaction data could not be read.";
        detailContent.innerHTML = `
            <div class="empty-state">${escapeHtml(error.message)}</div>
        `;
    }
}

initialize();
