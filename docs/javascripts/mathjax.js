// MathJax configuration for PostgreSQL Blockchain Extension documentation

window.MathJax = {
  tex: {
    inlineMath: [["\\(", "\\)"]],
    displayMath: [["\\[", "\\]"]],
    processEscapes: true,
    processEnvironments: true,
    tags: 'ams',
    packages: {'[+]': ['ams', 'color', 'bbox']}
  },
  options: {
    ignoreHtmlClass: ".*|",
    processHtmlClass: "arithmatex"
  },
  svg: {
    fontCache: 'global'
  },
  startup: {
    ready() {
      MathJax.startup.defaultReady();
      console.log('MathJax is loaded and ready for blockchain documentation');
    }
  }
};

// Custom macros for blockchain documentation
document$.subscribe(() => { 
  MathJax.startup.document.inputJax[0].parseOptions.macros = {
    // Hash function notation
    hash: "\\text{SHA256}",
    // Counter notation
    counter: "\\text{Counter}",
    // Chain notation
    chain: "\\rightarrow",
    // Performance metrics
    bigO: "\\mathcal{O}",
    // Time complexity
    timeComplexity: ["\\mathcal{O}(#1)", 1],
    // Space complexity
    spaceComplexity: ["\\mathcal{S}(#1)", 1]
  };
});