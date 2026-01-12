// Mermaid initialization for MkDocs Material
document$.subscribe(function() {
  // Initialize mermaid with Material theme
  mermaid.initialize({
    startOnLoad: true,
    theme: 'default',
    themeVariables: {
      primaryColor: '#2196F3',
      primaryTextColor: '#fff',
      primaryBorderColor: '#1976D2',
      lineColor: '#1976D2',
      secondaryColor: '#E3F2FD',
      tertiaryColor: '#BBDEFB'
    },
    flowchart: {
      useMaxWidth: true,
      htmlLabels: true,
      curve: 'basis'
    },
    sequence: {
      diagramMarginX: 50,
      diagramMarginY: 10,
      actorMargin: 50,
      width: 150,
      height: 65,
      boxMargin: 10,
      boxTextMargin: 5,
      noteMargin: 10,
      messageMargin: 35,
      mirrorActors: true,
      bottomMarginAdj: 1,
      useMaxWidth: true,
      rightAngles: false,
      showSequenceNumbers: false
    },
    gantt: {
      titleTopMargin: 25,
      barHeight: 20,
      barGap: 4,
      topPadding: 50,
      leftPadding: 75,
      gridLineStartPadding: 35,
      fontSize: 11,
      numberSectionStyles: 4,
      axisFormat: '%Y-%m-%d'
    }
  });

  // Re-render mermaid diagrams on page load
  if (typeof mermaid !== 'undefined') {
    mermaid.contentLoaded();
  }
});
