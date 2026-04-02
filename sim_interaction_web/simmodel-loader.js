/**
 * SimModel Loader Module
 * 
 * Dynamically loads all .simmodel files from the models/ directory
 * by querying a server-side endpoint that lists available models.
 * Falls back to a configurable list if the endpoint is unavailable.
 * 
 * Usage:
 *   const data = await SimModelLoader.loadModel('environmental_model_2d');
 *   const allModels = await SimModelLoader.loadAllModels();
 */

const SimModelLoader = {
    modelsCache: {},
    availableModels: null,
    apiEndpoint: 'api/models',

    /**
     * Discover available models by querying the server
     */
    async discoverModels() {
        if (this.availableModels) {
            return this.availableModels;
        }

        try {
            // Try to fetch the list of models from the server
            const response = await fetch(this.apiEndpoint, { cache: 'no-store' });
            if (response.ok) {
                this.availableModels = await response.json();
                console.log(`Discovered ${this.availableModels.length} models from server API`);
                return this.availableModels;
            } else {
                console.warn(`Server API returned ${response.status}, falling back to probing`);
            }
        } catch (error) {
            console.warn('Could not fetch models from server, using fallback discovery:', error.message);
        }

        // Fallback: try to discover models by attempting to fetch them
        this.availableModels = await this.discoverModelsByProbing();
        return this.availableModels;
    },

    /**
     * Discover models by probing for known model patterns
     */
    async discoverModelsByProbing() {
        const discovered = [];
        
        // Common model name patterns to try
        const patterns = [
            'environmental_model_2d',
            'game_of_life_model',
            'gray_scott_reaction_diffusion',
            'coastal_biogeochemistry_transport',
            'urban_microclimate_resilience'
        ];

        // Also try to discover by attempting to fetch model.json from common directories
        const potentialDirs = [
            'environmental_model_2d.simmodel',
            'game_of_life_model.simmodel',
            'gray_scott_reaction_diffusion.simmodel',
            'coastal_biogeochemistry_transport.simmodel',
            'urban_microclimate_resilience.simmodel'
        ];

        console.log('Probing for models in potential directories...');
        
        for (const dir of potentialDirs) {
            try {
                const response = await fetch(`models/${dir}/model.json`, { 
                    method: 'GET',
                    cache: 'no-store' 
                });
                if (response.ok) {
                    const modelId = dir.replace('.simmodel', '');
                    discovered.push({
                        id: modelId,
                        directory: dir
                    });
                    console.log(`Found model: ${modelId}`);
                }
            } catch (error) {
                // Model doesn't exist, skip
            }
        }

        if (discovered.length > 0) {
            console.log(`Discovered ${discovered.length} models by probing`);
            return discovered;
        }

        // If no models found, return empty array
        console.warn('No models discovered');
        return [];
    },

    /**
     * Get list of available models
     */
    async getAvailableModels() {
        const models = await this.discoverModels();
        return models.map(m => ({
            id: m.id,
            name: m.name || m.id,
            description: m.description || '',
            directory: m.directory
        }));
    },

    /**
     * Load a specific model by ID
     */
    async loadModel(modelId) {
        if (this.modelsCache[modelId]) {
            return this.modelsCache[modelId];
        }

        const available = await this.discoverModels();
        const modelInfo = available.find(m => m.id === modelId);
        
        if (!modelInfo) {
            throw new Error(`Model not found: ${modelId}`);
        }

        try {
            // Load model.json
            const modelResponse = await fetch(`models/${modelInfo.directory}/model.json`, { cache: 'no-store' });
            if (!modelResponse.ok) {
                throw new Error(`Unable to load model.json (${modelResponse.status})`);
            }
            const modelData = await modelResponse.json();

            // Load metadata.json
            let metadata = {};
            try {
                const metadataResponse = await fetch(`models/${modelInfo.directory}/metadata.json`, { cache: 'no-store' });
                if (metadataResponse.ok) {
                    metadata = await metadataResponse.json();
                }
            } catch (error) {
                console.warn(`Could not load metadata for ${modelId}`);
            }

            // Transform to website format
            const transformed = this.transformToWebsiteFormat(modelData, metadata);
            this.modelsCache[modelId] = transformed;
            
            return transformed;
        } catch (error) {
            console.error(`Failed to load model ${modelId}:`, error);
            throw error;
        }
    },

    /**
     * Load all available models
     */
    async loadAllModels() {
        const available = await this.discoverModels();
        const models = [];

        for (const modelInfo of available) {
            try {
                const modelData = await this.loadModel(modelInfo.id);
                models.push({
                    id: modelInfo.id,
                    name: modelInfo.name || modelData.metadata?.name || modelInfo.id,
                    description: modelInfo.description || modelData.metadata?.description || '',
                    data: modelData
                });
            } catch (error) {
                console.warn(`Skipping model ${modelInfo.id}:`, error);
            }
        }

        return models;
    },

    /**
     * Transform .simmodel data to website-compatible format
     */
    transformToWebsiteFormat(modelData, metadata) {
        const nodes = [];
        const interactions = [];
        const nodeIds = new Set();
        // Track ALL IDs (both nodes and interactions) to prevent duplicates
        const allIds = new Set();

        // Create nodes from variables
        if (modelData.variables) {
            for (const variable of modelData.variables) {
                // Skip comment blocks
                if (variable.__comment_block__) {
                    continue;
                }

                const nodeId = variable.id;
                // Skip if ID already exists in all IDs
                if (allIds.has(nodeId)) {
                    console.warn(`Skipping duplicate node ID: ${nodeId}`);
                    continue;
                }
                if (!nodeIds.has(nodeId)) {
                    nodeIds.add(nodeId);
                    allIds.add(nodeId);
                    
                    // Determine group based on role and support
                    let group = 'resource';
                    if (variable.support === 'global' && 
                        (variable.role === 'state' || variable.role === 'derived')) {
                        group = 'subsystem';
                    }

                    nodes.push({
                        id: nodeId,
                        label: variable.id,
                        group: group,
                        description: variable.description || '',
                        role: variable.role,
                        support: variable.support,
                        type: variable.type,
                        units: variable.units
                    });
                }
            }
        }

        // Create nodes from stages (as subsystems)
        if (modelData.stages) {
            for (const stage of modelData.stages) {
                const stageId = stage.id;
                // Skip if ID already exists in all IDs (could be a variable with same name)
                if (allIds.has(stageId)) {
                    console.warn(`Skipping duplicate stage ID: ${stageId} (already exists as node)`);
                    continue;
                }
                if (!nodeIds.has(stageId)) {
                    nodeIds.add(stageId);
                    allIds.add(stageId);
                    nodes.push({
                        id: stageId,
                        label: stage.id,
                        group: 'subsystem',
                        description: stage.description || ''
                    });
                }
            }
        }

        // Create interactions from stages
        if (modelData.stages) {
            // Track interaction IDs to prevent duplicates
            const interactionIds = new Set();
            
            for (const stage of modelData.stages) {
                if (!stage.interactions) continue;

                for (const interaction of stage.interactions) {
                    // Create interactions for each read variable
                    if (interaction.reads && interaction.reads.length > 0) {
                        for (const readVar of interaction.reads) {
                            // Skip parameters and constants
                            const readNode = nodes.find(n => n.id === readVar);
                            if (readNode && readNode.role === 'parameter') {
                                continue;
                            }

                            // Create one interaction per write variable
                            if (interaction.writes && interaction.writes.length > 0) {
                                for (const writeVar of interaction.writes) {
                                    const interactionId = `${stage.id}_${interaction.id}_${readVar}`;
                                    
                                    // Skip if this interaction ID already exists
                                    if (interactionIds.has(interactionId)) {
                                        console.warn(`Duplicate interaction ID: ${interactionId}, skipping`);
                                        continue;
                                    }
                                    interactionIds.add(interactionId);
                                    
                                    interactions.push({
                                        id: interactionId,
                                        source: readVar,
                                        target: writeVar,
                                        mode: 'read',
                                        summary: this.generateSummary(readVar, writeVar, stage.id),
                                        detail: stage.description || '',
                                        equation: '',
                                        evidence: stage.id,
                                        stageId: stage.id,
                                        interactionId: interaction.id,
                                        targetType: interaction.target_type
                                    });
                                }
                            }
                        }
                    }

                    // Also create write interactions from stage to write variables
                    if (interaction.writes && interaction.writes.length > 0) {
                        for (const writeVar of interaction.writes) {
                            const interactionId = `${stage.id}_${interaction.id}_write_${writeVar}`;
                            
                            // Skip if this interaction ID already exists
                            if (interactionIds.has(interactionId)) {
                                console.warn(`Duplicate interaction ID: ${interactionId}, skipping`);
                                continue;
                            }
                            interactionIds.add(interactionId);
                            
                            interactions.push({
                                id: interactionId,
                                source: stage.id,
                                target: writeVar,
                                mode: 'write',
                                summary: `${stage.id} writes to ${writeVar}`,
                                detail: stage.description || '',
                                equation: '',
                                evidence: stage.id,
                                stageId: stage.id,
                                interactionId: interaction.id,
                                targetType: interaction.target_type
                            });
                        }
                    }
                }
            }
        }

        return {
            nodes,
            interactions,
            metadata: {
                id: modelData.id,
                name: metadata.name || modelData.id,
                description: metadata.description || '',
                version: modelData.version || '1.0.0',
                author: metadata.author || '',
                tags: metadata.tags || []
            }
        };
    },

    /**
     * Generate a human-readable summary for an interaction
     */
    generateSummary(source, target, stageId) {
        // Clean up variable names for display
        const sourceLabel = source.replace(/_/g, ' ');
        const targetLabel = target.replace(/_/g, ' ');
        
        return `${sourceLabel} contributes to ${targetLabel}`;
    },

    /**
     * Clear the cache
     */
    clearCache() {
        this.modelsCache = {};
        this.availableModels = null;
    }
};

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = SimModelLoader;
}
