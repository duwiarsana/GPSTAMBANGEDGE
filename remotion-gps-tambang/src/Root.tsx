import React from 'react';
import { Composition } from 'remotion';
import { GpsTambangVideo } from './GpsTambangVideo';

export const RemotionRoot: React.FC = () => {
  return (
    <>
      <Composition
        id="GpsTambangEdge"
        component={GpsTambangVideo}
        durationInFrames={1800} // 60s
        fps={30}
        width={1080}
        height={1920}
      />
    </>
  );
};
